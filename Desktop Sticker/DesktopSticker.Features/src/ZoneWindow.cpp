#include "pch.h"
#include "desktopsticker/ZoneWindow.h"
#include "desktopsticker/IconService.h"
#include "desktopsticker/Log.h"
#include "desktopsticker/ZoneGrid.h"

#include <cstdlib>
#include <dwrite.h>
#include <map>

namespace desktopsticker {

namespace {
const wchar_t kZoneWindowClass[] = L"DesktopSticker.ZoneWindow";
constexpr UINT_PTR kRenderTimerId = 1; // 统一渲染/滚动插值定时器（~60fps）
std::map<HWND, ZoneWindow*> g_windows;

void ApplyAcrylic(HWND hwnd) {
    enum AccentState { ACCENT_DISABLED = 0, ACCENT_ENABLE_BLURBEHIND = 3, ACCENT_ENABLE_ACRYLICBLURBEHIND = 4 };
    struct AccentPolicy { int state; int flags; int color; int animationId; };
    struct WinCompAttrData { int attribute; void* data; unsigned long size; };

    using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WinCompAttrData*);
    static SetWindowCompositionAttributeFn fn = nullptr;
    if (!fn) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        fn = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
    }
    if (!fn) return;

    AccentPolicy policy{};
    policy.state = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.flags = 2;
    policy.color = 0xCC1E1E1E; // BGRA：半透明深色
    WinCompAttrData data{};
    data.attribute = 19; // WCA_ACCENT_POLICY
    data.data = &policy;
    data.size = sizeof(policy);
    fn(hwnd, &data);
}
} // namespace

bool ZoneWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS; // 支持双击（WM_LBUTTONDBLCLK）
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kZoneWindowClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void ZoneWindow::UnregisterClass(HINSTANCE hInst) {
    ::UnregisterClassW(kZoneWindowClass, hInst);
}

ZoneWindow* ZoneWindow::FromHwnd(HWND hwnd) {
    auto it = g_windows.find(hwnd);
    return it == g_windows.end() ? nullptr : it->second;
}

ZoneWindow::ZoneWindow(HINSTANCE hInst, const Zone& zone, IconService* icons,
                       int columnSpacing, int rowSpacing)
    : hInst_(hInst), zone_(zone), icons_(icons),
      columnSpacing_(columnSpacing), rowSpacing_(rowSpacing) {}

ZoneWindow::~ZoneWindow() {
    Destroy();
    ReleaseD2DResources();
}

bool ZoneWindow::Create() {
    DWORD style = WS_POPUP | WS_VISIBLE | WS_THICKFRAME;
    // 分层窗口 + UpdateLayeredWindow 渲染，才能实现半透明 Acrylic 磨砂
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    hwnd_ = CreateWindowExW(exStyle, kZoneWindowClass, L"",
                            style,
                            zone_.rect.left, zone_.rect.top,
                            zone_.rect.right - zone_.rect.left,
                            zone_.rect.bottom - zone_.rect.top,
                            nullptr, nullptr, hInst_, this);
    if (!hwnd_) return false;

    g_windows[hwnd_] = this;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // 圆角外形与点击命中由逐像素 Alpha（ULW 位图）决定；不能调用 SetLayeredWindowAttributes，
    // 一旦调用窗口就切到 SLWA 模式，UpdateLayeredWindow 将失效（本机该模式不渲染）
    ApplyAcrylic(hwnd_);
    return true;
}

void ZoneWindow::Destroy() {
    if (hwnd_ && IsWindow(hwnd_)) {
        if (renderTimer_) {
            KillTimer(hwnd_, kRenderTimerId);
            renderTimer_ = 0;
        }
        g_windows.erase(hwnd_);
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    if (paintBmp_) {
        DeleteObject(paintBmp_);
        paintBmp_ = nullptr;
        paintBits_ = nullptr;
    }
    paintW_ = paintH_ = 0;
}

void ZoneWindow::SetZone(const Zone& zone) {
    if (zone_.collapsed != zone.collapsed || zone_.name != zone.name ||
        zone_.itemPaths != zone.itemPaths) {
        contentDirty_ = true; // 数据变化时重建内容层缓存
    }
    zone_ = zone;
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, zone_.rect.left, zone_.rect.top,
                     zone_.rect.right - zone_.rect.left,
                     zone_.rect.bottom - zone_.rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Refresh();
    }
}

void ZoneWindow::SetSpacing(int columnSpacing, int rowSpacing) {
    columnSpacing_ = columnSpacing;
    rowSpacing_ = rowSpacing;
    scrollOffset_ = 0;
    scrollTarget_ = 0;
    contentDirty_ = true;
    Refresh();
}

void ZoneWindow::Refresh() {
    // 重绘请求合并：只置标志并确保 ~60fps 渲染定时器在跑，避免 hover/滚动时重绘堆积
    refreshPending_ = true;
    if (hwnd_ && IsWindow(hwnd_) && !renderTimer_) {
        renderTimer_ = SetTimer(hwnd_, kRenderTimerId, 16, nullptr);
    }
}

void ZoneWindow::RenderTick() {
    // 滚动插值：每帧向目标位置指数趋近（kScrollEase，至少 1px），尾部自然减速
    const int diff = scrollTarget_ - scrollOffset_;
    if (diff != 0) {
        int step = static_cast<int>(diff * zoneui::kScrollEase);
        if (step == 0) step = diff > 0 ? 1 : -1;
        scrollOffset_ += step;
        if (std::abs(scrollTarget_ - scrollOffset_) <= 1) scrollOffset_ = scrollTarget_;
    }
    OnPaint();
    refreshPending_ = false;
    if (!refreshPending_ && scrollTarget_ == scrollOffset_ && renderTimer_) {
        KillTimer(hwnd_, kRenderTimerId);
        renderTimer_ = 0;
    }
}

void ZoneWindow::RenderContentCache(int width, int contentHeight) {
    if (!target_) return;
    // BitmapRenderTarget 无 Resize：尺寸变化时整体重建（其上的图标位图缓存一并失效）
    if (contentRt_ && (contentW_ != width || contentH_ != contentHeight)) {
        for (auto& [path, bmp] : bitmapCache_) {
            if (bmp) bmp->Release();
        }
        bitmapCache_.clear();
        contentRt_->Release();
        contentRt_ = nullptr;
    }
    if (!contentRt_) {
        const D2D1_SIZE_F size = D2D1::SizeF(static_cast<float>(width), static_cast<float>(contentHeight));
        target_->CreateCompatibleRenderTarget(size, &contentRt_);
        if (!contentRt_) return;
    }

    contentRt_->BeginDraw();
    contentRt_->Clear(D2D1::ColorF(0, 0)); // 透明底，卡片底色由帧层绘制

    // 网格位置统一由 ZoneGrid 计算（与命中测试/滚动上限同一份实现）
    const int cols = zoneui::ColumnsForWidth(static_cast<float>(width), columnSpacing_);
    for (size_t itemIndex = 0; itemIndex < zone_.itemPaths.size(); ++itemIndex) {
        const auto& path = zone_.itemPaths[itemIndex];
        const float x = zoneui::CellX(static_cast<int>(itemIndex) % cols, columnSpacing_);
        const float y = zoneui::CellY(static_cast<int>(itemIndex) / cols, rowSpacing_);
        // 悬停/按下高亮（画进内容层，随内容一起滚动）
        if (path == hoverItem_ || path == pressedItem_) {
            contentRt_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x - 4, y - 4, x + 36, y + 36), 8.0f, 8.0f),
                hoverBrush_);
        }
        HICON icon = icons_ ? icons_->GetIcon(path, 32) : nullptr;
        if (icon) {
            ID2D1Bitmap* bmp = nullptr;
            auto cached = bitmapCache_.find(path);
            if (cached != bitmapCache_.end()) {
                bmp = cached->second;
            } else {
                IWICBitmap* wicBmp = nullptr;
                if (SUCCEEDED(wicFactory_->CreateBitmapFromHICON(icon, &wicBmp))) {
                    IWICFormatConverter* converter = nullptr;
                    if (SUCCEEDED(wicFactory_->CreateFormatConverter(&converter))) {
                        if (SUCCEEDED(converter->Initialize(
                                wicBmp, GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
                            ID2D1Bitmap* created = nullptr;
                            if (SUCCEEDED(contentRt_->CreateBitmapFromWicBitmap(converter, nullptr, &created))) {
                                bitmapCache_[path] = created; // 缓存持有，ReleaseD2DResources 统一释放
                                bmp = created;
                            }
                        }
                        converter->Release();
                    }
                    wicBmp->Release();
                }
            }
            if (bmp) {
                contentRt_->DrawBitmap(bmp, D2D1::RectF(x, y, x + zoneui::kIconSize, y + zoneui::kIconSize),
                                       1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            }
        } else {
            contentRt_->FillRectangle(D2D1::RectF(x, y, x + zoneui::kIconSize, y + zoneui::kIconSize), bgBrush_);
        }
        // 名称：最多 3 行，超长省略号；布局按路径缓存，避免每帧重建（重建成本很高）
        const std::wstring name = std::filesystem::path(path).stem().wstring();
        IDWriteTextLayout* layout = nullptr;
        auto layoutIt = textLayoutCache_.find(path);
        if (layoutIt != textLayoutCache_.end()) {
            layout = layoutIt->second;
        } else if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                       name.c_str(), static_cast<UINT32>(name.size()), labelFormat_,
                       44.0f, 33.0f, &layout)) && layout) {
            DWRITE_TRIMMING trimming{};
            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            layout->SetTrimming(&trimming, nullptr);
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            // 对齐方式与旧版一致：含空格两端对齐、短名居中、其余左对齐
            if (name.find(L' ') != std::wstring::npos) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_JUSTIFIED);
            } else if (name.size() <= 4) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            textLayoutCache_[path] = layout;
        }
        if (layout) {
            contentRt_->DrawTextLayout(D2D1::Point2F(x - 6, y + zoneui::kLabelTop), layout, labelBrush_);
        }
    }
    contentRt_->EndDraw();

    if (contentBmp_) contentBmp_->Release();
    contentBmp_ = nullptr;
    contentRt_->GetBitmap(&contentBmp_);
    contentW_ = width;
    contentH_ = contentHeight;
    contentDirty_ = false;
}

std::wstring ZoneWindow::HitTestItem(int x, int y) const {
    if (zone_.collapsed) return L"";
    const float width = static_cast<float>(zone_.rect.right - zone_.rect.left);
    const int cols = zoneui::ColumnsForWidth(width, columnSpacing_);
    const float tileYTop = zoneui::kTitleBand - static_cast<float>(scrollOffset_);
    for (size_t i = 0; i < zone_.itemPaths.size(); ++i) {
        const float tileX = zoneui::CellX(static_cast<int>(i) % cols, columnSpacing_);
        const float tileY = tileYTop + zoneui::CellY(static_cast<int>(i) / cols, rowSpacing_);
        if (x >= tileX && x <= tileX + zoneui::kIconSize &&
            y >= tileY && y <= tileY + zoneui::kIconSize) {
            return zone_.itemPaths[i];
        }
    }
    return L"";
}

bool ZoneWindow::EnsureD2DResources() {
    if (factory_ && dwriteFactory_ && wicFactory_ && target_) return true;
    if (!factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwriteFactory_));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wicFactory_));
        if (!factory_ || !dwriteFactory_ || !wicFactory_) return false;
    }
    if (!target_) {
        // DC 渲染目标：B8G8R8A8 预乘 Alpha，BindDC 到内存 DIB 后由 UpdateLayeredWindow 合成
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        factory_->CreateDCRenderTarget(&props, &target_);
        if (!target_) return false;
    }
    // Win11 Fluent 风格：柔和深灰亚克力底、低透明度描边、次要文字降透明度
    if (!bgBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0x2B2B2B, 0.72f), &bgBrush_);
    if (!titleBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.96f), &titleBrush_);
    if (!labelBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.82f), &labelBrush_);
    if (!borderBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.12f), &borderBrush_);
    if (!hoverBrush_) target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.09f), &hoverBrush_);
    if (!textFormat_) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         14.0f, L"zh-cn", &textFormat_);
    }
    if (!labelFormat_) {
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         11.0f, L"zh-cn", &labelFormat_);
    }
    return true;
}

void ZoneWindow::ReleaseD2DResources() {
    for (auto& [path, layout] : textLayoutCache_) {
        if (layout) layout->Release();
    }
    textLayoutCache_.clear();
    for (auto& [path, bmp] : bitmapCache_) {
        if (bmp) bmp->Release();
    }
    bitmapCache_.clear();
    if (contentBmp_) contentBmp_->Release();
    contentBmp_ = nullptr;
    if (contentRt_) contentRt_->Release();
    contentRt_ = nullptr;
    contentW_ = contentH_ = 0;
    contentDirty_ = true;
    if (labelFormat_) labelFormat_->Release();
    if (textFormat_) textFormat_->Release();
    if (hoverBrush_) hoverBrush_->Release();
    if (borderBrush_) borderBrush_->Release();
    if (labelBrush_) labelBrush_->Release();
    if (titleBrush_) titleBrush_->Release();
    if (bgBrush_) bgBrush_->Release();
    if (target_) target_->Release();
    if (dwriteFactory_) dwriteFactory_->Release();
    if (wicFactory_) wicFactory_->Release();
    if (factory_) factory_->Release();
    labelFormat_ = nullptr; textFormat_ = nullptr;
    hoverBrush_ = nullptr; borderBrush_ = nullptr; labelBrush_ = nullptr;
    titleBrush_ = nullptr; bgBrush_ = nullptr;
    target_ = nullptr; dwriteFactory_ = nullptr; wicFactory_ = nullptr; factory_ = nullptr;
}

void ZoneWindow::OnPaint() {
    if (!EnsureD2DResources()) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    // 分层窗口：渲染到 32bpp 内存 DIB，再用 UpdateLayeredWindow 合成（本机唯一可见的路径）。
    // DIB 按尺寸复用，滚动动画每帧重绘时避免反复分配大块内存
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    if (!paintBmp_ || paintW_ != w || paintH_ != h) {
        if (paintBmp_) {
            DeleteObject(paintBmp_);
            paintBmp_ = nullptr;
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        paintBmp_ = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &paintBits_, nullptr, 0);
        if (!paintBmp_) {
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return;
        }
        paintW_ = w;
        paintH_ = h;
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, paintBmp_);

    target_->BindDC(hdcMem, &rc);
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0)); // 全透明底：圆角外透空

    const float width = static_cast<float>(w);
    const float height = static_cast<float>(h);

    // 内容层失效检测：数据/尺寸/间距变化才重建，滚动帧只做一次位图平移
    int contentH = 0;
    if (!zone_.collapsed) {
        const int cols = zoneui::ColumnsForWidth(width, columnSpacing_);
        const int rows = zoneui::RowCountFor(zone_.itemPaths.size(), cols);
        contentH = zoneui::ContentHeightFor(rows, rowSpacing_); // 含末行名称底部
        if (contentDirty_ || contentW_ != w || contentH_ != contentH) {
            RenderContentCache(w, contentH);
            contentH = contentH_;
        }
    }

    // 卡片半透明底色（固定层），磁贴内容层在其上按滚动偏移平移
    // Win11 圆角：背景、描边、磁贴高亮统一
    target_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(1, 1, width - 1, height - 1),
                          zoneui::kCornerRadius, zoneui::kCornerRadius), bgBrush_);
    if (!zone_.collapsed && contentBmp_) {
        const float yTop = zoneui::kTitleBand - static_cast<float>(scrollOffset_);
        target_->DrawBitmap(contentBmp_,
                            D2D1::RectF(0, yTop, width, yTop + static_cast<float>(contentH_)));
    }

    std::wstring title = zone_.collapsed ? L"\x25B8 " + zone_.name : L"\x25BE " + zone_.name;
    target_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), textFormat_,
                       D2D1::RectF(16, 8, 400, 40), titleBrush_);
    target_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f),
                          zoneui::kCornerRadius, zoneui::kCornerRadius),
        borderBrush_, 1.0f);

    const HRESULT endHr = target_->EndDraw();

    if (SUCCEEDED(endHr)) {
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255; // 半透明由逐像素 Alpha 携带
        blend.AlphaFormat = AC_SRC_ALPHA;
        POINT ptDst{};
        RECT winRect{};
        GetWindowRect(hwnd_, &winRect);
        ptDst.x = winRect.left;
        ptDst.y = winRect.top;
        HWND parent = GetAncestor(hwnd_, GA_PARENT);
        if (parent && parent != GetDesktopWindow()) {
            ScreenToClient(parent, &ptDst); // 子窗口的 UpdateLayeredWindow 位置相对父客户区
        }
        SIZE size{w, h};
        POINT ptSrc{0, 0};
        UpdateLayeredWindow(hwnd_, hdcScreen, &ptDst, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    } else {
        // 设备丢失（如 D2DERR_RECREATE_TARGET）：释放资源，下次绘制时重建，否则永久黑屏
        ReleaseD2DResources();
    }

    SelectObject(hdcMem, oldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void ZoneWindow::OnLButtonDown(int x, int y) {
    const std::wstring hit = HitTestItem(x, y);
    GetCursorPos(&dragStart_);
    if (!hit.empty()) {
        // 按在磁贴上：拖动磁贴（松手时落点决定换分区或恢复为桌面图标）
        tileDragging_ = true;
        dragging_ = false;
        draggingItem_ = hit;
        pressedItem_ = hit;
        dragMoved_ = false;
        contentDirty_ = true;
    } else {
        // 按在标题/空白：移动整张卡片（折叠/展开改为双击标题触发，避免单击拖拽冲突）
        tileDragging_ = false;
        dragging_ = true;
        draggingItem_.clear();
        pressedItem_.clear();
        dragMoved_ = false;
        // 必须记录“屏幕坐标”作为拖拽起点，否则窗口会按自身位置向下/右偏移
        GetWindowRect(hwnd_, &windowStart_);
    }
    // 嵌入模式下延迟到移动阈值再 SetCapture，保证系统还能合成 WM_LBUTTONDBLCLK；
    // 降级模式（最底层窗口）收不到移动消息，必须立即捕获（双击由 hook 合成，不依赖系统）
    if (!embedded_) {
        SetCapture(hwnd_);
        captureSet_ = true;
    } else {
        captureSet_ = false;
    }
    Refresh();
}

void ZoneWindow::OnLButtonUp(int x, int y) {
    if (tileDragging_) {
        tileDragging_ = false;
        if (dragMoved_ && onTileDrop) {
            POINT pt{};
            GetCursorPos(&pt);
            onTileDrop(zone_.id, draggingItem_, pt);
        }
        draggingItem_.clear();
        pressedItem_.clear();
        contentDirty_ = true;
        if (captureSet_) ReleaseCapture();
        captureSet_ = false;
        Refresh();
        return;
    }
    if ((dragging_ || resizing_) && geometryDirty_ && onGeometryChanged) {
        onGeometryChanged(zone_.id, zone_.rect); // 拖动/缩放落盘，重启后位置保持
    }
    geometryDirty_ = false;
    dragging_ = false;
    resizing_ = false;
    resizeHit_ = 0;
    dragMoved_ = false;
    pressedItem_.clear();
    draggingItem_.clear();
    contentDirty_ = true;
    if (captureSet_) ReleaseCapture();
    captureSet_ = false;
    Refresh();
}

void ZoneWindow::StartResize(int hitCode) {
    resizing_ = true;
    resizeHit_ = hitCode;
    GetCursorPos(&dragStart_);
    GetWindowRect(hwnd_, &windowStart_);
    SetCapture(hwnd_);
    captureSet_ = true;
}

void ZoneWindow::OnMouseWheel(int delta) {
    if (zone_.collapsed) return;
    // 高分辨率滚轮/触摸板以小于 WHEEL_DELTA 的增量高频上报，
    // 整数除法会把小增量全部截断成 0（表现为“滚不动”），这里累积余数
    wheelRemainder_ += delta;
    const int notches = wheelRemainder_ / WHEEL_DELTA;
    wheelRemainder_ -= notches * WHEEL_DELTA;
    if (notches == 0) return;

    const float width = static_cast<float>(zone_.rect.right - zone_.rect.left);
    const float height = static_cast<float>(zone_.rect.bottom - zone_.rect.top);
    const int cols = zoneui::ColumnsForWidth(width, columnSpacing_);
    const int rows = zoneui::RowCountFor(zone_.itemPaths.size(), cols);
    const int maxScroll = zoneui::MaxScrollFor(height, rows, rowSpacing_);

    // 滚轮只更新目标位置，由渲染定时器插值逼近，实现平滑滚动
    scrollTarget_ -= notches * zoneui::kScrollStep;
    scrollTarget_ = std::max(0, std::min(scrollTarget_, maxScroll));
    Refresh();
}

void ZoneWindow::OnMouseMove(int x, int y) {
    POINT pt{};
    GetCursorPos(&pt);
    const int dx = pt.x - dragStart_.x;
    const int dy = pt.y - dragStart_.y;

    if (resizing_) {
        const bool left = resizeHit_ == HTLEFT || resizeHit_ == HTTOPLEFT || resizeHit_ == HTBOTTOMLEFT;
        const bool right = resizeHit_ == HTRIGHT || resizeHit_ == HTTOPRIGHT || resizeHit_ == HTBOTTOMRIGHT;
        const bool top = resizeHit_ == HTTOP || resizeHit_ == HTTOPLEFT || resizeHit_ == HTTOPRIGHT;
        const bool bottom = resizeHit_ == HTBOTTOM || resizeHit_ == HTBOTTOMLEFT || resizeHit_ == HTBOTTOMRIGHT;

        RECT r = windowStart_;
        if (left) r.left += dx;
        if (right) r.right += dx;
        if (top) r.top += dy;
        if (bottom) r.bottom += dy;

        const int minW = zoneui::kMinCardW;
        const int minH = zoneui::kMinCardH;
        if (r.right - r.left < minW) {
            if (left) r.left = r.right - minW;
            else r.right = r.left + minW;
        }
        if (r.bottom - r.top < minH) {
            if (top) r.top = r.bottom - minH;
            else r.bottom = r.top + minH;
        }

        POINT newPos{r.left, r.top};
        HWND parent = GetAncestor(hwnd_, GA_PARENT);
        if (parent && parent != GetDesktopWindow()) {
            ScreenToClient(parent, &newPos);
        }
        SetWindowPos(hwnd_, nullptr, newPos.x, newPos.y,
                     r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        zone_.rect = r;
        if (dx != 0 || dy != 0) geometryDirty_ = true;
        Refresh(); // 尺寸变化后重新布局换行（ULW 按新客户区尺寸重建 DIB）
        return;
    }

    if (tileDragging_) {
        if (!captureSet_ && std::abs(dx) + std::abs(dy) > 4) {
            SetCapture(hwnd_);
            captureSet_ = true;
        }
        if (std::abs(dx) + std::abs(dy) > 8) dragMoved_ = true;
        return; // 松手时由 onTileDrop 按落点决定去向
    }

    if (!dragging_) {
        // 悬停高亮（高亮画进内容层，变化时标记缓存重建）
        const std::wstring hit = HitTestItem(x, y);
        if (hit != hoverItem_) {
            hoverItem_ = hit;
            contentDirty_ = true;
            Refresh();
        }
        return;
    }

    // 嵌入模式：延迟捕获（见 OnLButtonDown），移动超过阈值后再接管鼠标
    if (!captureSet_ && std::abs(dx) + std::abs(dy) > 4) {
        SetCapture(hwnd_);
        captureSet_ = true;
    }
    if (std::abs(dx) + std::abs(dy) > 8) dragMoved_ = true;

    // 按住卡片任意非磁贴位置（含标题）移动整个分区
    POINT newPos{windowStart_.left + dx, windowStart_.top + dy};
    // WS_CHILD 子窗口的 SetWindowPos 使用父客户区坐标
    HWND parent = GetAncestor(hwnd_, GA_PARENT);
    if (parent && parent != GetDesktopWindow()) {
        ScreenToClient(parent, &newPos);
    }
    SetWindowPos(hwnd_, nullptr, newPos.x, newPos.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    zone_.rect = RECT{windowStart_.left + dx, windowStart_.top + dy,
                      windowStart_.right + dx, windowStart_.bottom + dy};
    if (dx != 0 || dy != 0) geometryDirty_ = true;
}

void ZoneWindow::OnRButtonUp(int x, int y) {
    POINT pt{x, y};
    ClientToScreen(hwnd_, &pt);
    HMENU menu = CreatePopupMenu();
    std::wstring item = HitTestItem(x, y);

    if (y < zoneui::kTitleHit) {
        AppendMenuW(menu, MF_STRING, 1, L"重命名");
        AppendMenuW(menu, MF_STRING, 2, L"删除分区");
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        if (cmd == 1 && onRenameZone) onRenameZone(zone_.id);
        if (cmd == 2 && onDeleteZone) onDeleteZone(zone_.id);
    } else if (!item.empty()) {
        AppendMenuW(menu, MF_STRING, 1, L"打开");
        AppendMenuW(menu, MF_STRING, 2, L"从分区移出");
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        if (cmd == 1) {
            ShellExecuteW(nullptr, L"open", item.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (cmd == 2 && onRemoveItem) onRemoveItem(zone_.id, item);
    }
    DestroyMenu(menu);
}

LRESULT CALLBACK ZoneWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ZoneWindow* self = FromHwnd(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ZoneWindow*>(cs->lpCreateParams);
        g_windows[hwnd] = self;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT:
        self->OnPaint();
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == kRenderTimerId) {
            self->RenderTick();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONDBLCLK: {
        const int cx = GET_X_LPARAM(lp);
        const int cy = GET_Y_LPARAM(lp);
        if (cy < zoneui::kTitleHit) {
            // 标题栏双击：折叠/展开（单击标题保留给拖动卡片）
            if (self->onCollapseToggle) self->onCollapseToggle(self->zone_.id);
            return 0;
        }
        const std::wstring item = self->HitTestItem(cx, cy);
        if (!item.empty()) {
            const HINSTANCE se = ShellExecuteW(nullptr, L"open", item.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            const DWORD_PTR code = reinterpret_cast<DWORD_PTR>(se);
            if (code <= 32) {
                // 打开失败：记录返回码便于定位（>32 为成功句柄）
                dstklog::Write(L"open-fail", L"zone=" + self->zone_.name +
                             L" se=" + std::to_wstring(code) +
                             L" gle=" + std::to_wstring(GetLastError()) +
                             L" path=" + item);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        self->OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        self->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_RBUTTONUP:
        self->OnRButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_NCLBUTTONDOWN: {
        const int hit = static_cast<int>(wp);
        if (hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM ||
            hit == HTTOPLEFT || hit == HTTOPRIGHT || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT) {
            self->StartResize(hit);
            return 0;
        }
        break;
    }
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (pt.y < zoneui::kTitleHit || !self->HitTestItem(pt.x, pt.y).empty()) return HTCLIENT;
        const LONG width = self->GetZone().rect.right - self->GetZone().rect.left;
        const LONG height = self->GetZone().rect.bottom - self->GetZone().rect.top;
        const int edge = zoneui::kResizeEdge;
        bool left = pt.x <= edge, right = pt.x >= width - edge;
        bool top = pt.y <= edge, bottom = pt.y >= height - edge;
        if (left && top) return HTTOPLEFT;
        if (right && top) return HTTOPRIGHT;
        if (left && bottom) return HTBOTTOMLEFT;
        if (right && bottom) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        // 空白区域也返回 HTCLIENT，让整张卡片都能拖动
        return HTCLIENT;
    }
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_DESTROY:
        g_windows.erase(hwnd);
        if (self) self->hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace desktopsticker
