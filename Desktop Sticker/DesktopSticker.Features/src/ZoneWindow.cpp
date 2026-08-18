#include "pch.h"
#include "desktopsticker/ZoneWindow.h"
#include "desktopsticker/IconService.h"
#include "desktopsticker/Utf8.h"

#include <cstdlib>
#include <dwrite.h>
#include <fstream>
#include <map>
#include <shlobj.h>

namespace desktopsticker {

namespace {
const wchar_t kZoneWindowClass[] = L"DesktopSticker.ZoneWindow";
const UINT kZoneRefreshMsg = WM_APP + 10;
std::map<HWND, ZoneWindow*> g_windows;

void ZoneDebugLog(const std::wstring& msg) {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return;
    std::filesystem::path root(appData);
    CoTaskMemFree(appData);
    root /= L"DesktopSticker";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::ofstream out(root / L"debug.log", std::ios::app);
    out << ToUtf8(msg) << std::endl;
}

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

ZoneWindow::ZoneWindow(HINSTANCE hInst, const Zone& zone, IconService* icons)
    : hInst_(hInst), zone_(zone), icons_(icons) {}

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

    ApplyAcrylic(hwnd_);
    return true;
}

void ZoneWindow::Destroy() {
    if (hwnd_ && IsWindow(hwnd_)) {
        g_windows.erase(hwnd_);
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
}

void ZoneWindow::SetZone(const Zone& zone) {
    zone_ = zone;
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, zone_.rect.left, zone_.rect.top,
                     zone_.rect.right - zone_.rect.left,
                     zone_.rect.bottom - zone_.rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void ZoneWindow::Refresh() {
    // 分层窗口不会因 InvalidateRect 自动重绘，投递自定义消息让 UI 线程调用 OnPaint
    if (hwnd_) PostMessageW(hwnd_, kZoneRefreshMsg, 0, 0);
}

std::wstring ZoneWindow::HitTestItem(int x, int y) const {
    if (zone_.collapsed) return L"";
    float tileX = 16.0f;
    float tileY = 48.0f;
    for (const auto& path : zone_.itemPaths) {
        if (x >= tileX && x <= tileX + 32 && y >= tileY && y <= tileY + 32) {
            return path;
        }
        tileX += 48.0f;
        const float width = static_cast<float>(zone_.rect.right - zone_.rect.left);
        if (tileX + 48 > width) {
            tileX = 16.0f;
            tileY += 56.0f;
        }
    }
    return L"";
}

bool ZoneWindow::EnsureD2DResources() {
    if (factory_ && dwriteFactory_ && wicFactory_ && dcTarget_) return true;
    if (!factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwriteFactory_));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wicFactory_));
        if (!factory_ || !dwriteFactory_ || !wicFactory_) return false;
    }
    if (!dcTarget_) {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        factory_->CreateDCRenderTarget(&props, &dcTarget_);
        if (!dcTarget_) return false;
    }
    if (!bgBrush_) dcTarget_->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E, 0.78f), &bgBrush_);
    if (!titleBrush_) dcTarget_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), &titleBrush_);
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
    if (labelFormat_) labelFormat_->Release();
    if (textFormat_) textFormat_->Release();
    if (titleBrush_) titleBrush_->Release();
    if (bgBrush_) bgBrush_->Release();
    if (dcTarget_) dcTarget_->Release();
    if (dwriteFactory_) dwriteFactory_->Release();
    if (wicFactory_) wicFactory_->Release();
    if (factory_) factory_->Release();
    labelFormat_ = nullptr; textFormat_ = nullptr; titleBrush_ = nullptr; bgBrush_ = nullptr;
    dcTarget_ = nullptr; dwriteFactory_ = nullptr; wicFactory_ = nullptr; factory_ = nullptr;
}

void ZoneWindow::OnPaint() {
    if (!EnsureD2DResources()) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    // 分层窗口：渲染到 32bpp 内存 DIB，再用 UpdateLayeredWindow 合成（支持 Alpha 和 Acrylic）
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);

    dcTarget_->BindDC(hdcMem, &rc);
    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1::ColorF(0, 0)); // 全透明，让 Acrylic 模糊透出

    const float width = static_cast<float>(w);
    const float height = static_cast<float>(h);
    dcTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(1, 1, width - 1, height - 1), 16.0f, 16.0f),
        bgBrush_);

    std::wstring title = zone_.collapsed ? L"\x25B8 " + zone_.name : L"\x25BE " + zone_.name;
    dcTarget_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), textFormat_,
                         D2D1::RectF(16, 8, 400, 40), titleBrush_);

    if (!zone_.collapsed) {
        float x = 16.0f;
        float y = 48.0f;
        int total = 0, iconOk = 0, wicOk = 0;
        for (const auto& path : zone_.itemPaths) {
            ++total;
            HICON icon = icons_ ? icons_->GetIcon(path, 32) : nullptr;
            if (icon) {
                ++iconOk;
                // 用 WIC 把 HICON 转成 D2D 位图再绘制（GDI DrawIconEx 在 D2D 表面上不显示）
                IWICBitmap* wicBmp = nullptr;
                if (SUCCEEDED(wicFactory_->CreateBitmapFromHICON(icon, &wicBmp))) {
                    // D2D 需要 32bpp 预乘 alpha，WIC 图标可能不是该格式，先转换
                    IWICFormatConverter* converter = nullptr;
                    if (SUCCEEDED(wicFactory_->CreateFormatConverter(&converter))) {
                        if (SUCCEEDED(converter->Initialize(
                                wicBmp, GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
                            ID2D1Bitmap* d2dBmp = nullptr;
                            const HRESULT d2dHr = dcTarget_->CreateBitmapFromWicBitmap(converter, nullptr, &d2dBmp);
                            if (SUCCEEDED(d2dHr)) {
                                ++wicOk;
                                dcTarget_->DrawBitmap(d2dBmp, D2D1::RectF(x, y, x + 32, y + 32),
                                                     1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                                d2dBmp->Release();
                            }
                        }
                        converter->Release();
                    }
                    wicBmp->Release();
                }
            } else {
                dcTarget_->FillRectangle(D2D1::RectF(x, y, x + 32, y + 32), bgBrush_);
            }
            // 图标下方显示名称
            {
                const std::wstring name = std::filesystem::path(path).stem().wstring();
                dcTarget_->DrawTextW(name.c_str(), static_cast<UINT32>(name.size()), labelFormat_,
                                     D2D1::RectF(x - 6, y + 34, x + 38, y + 50), titleBrush_);
            }
            x += 48.0f;
            if (x + 48 > width) {
                x = 16.0f;
                y += 56.0f;
            }
        }
        static bool s_paintLogged = false;
        if (!s_paintLogged) {
            ZoneDebugLog(L"[zone paint] zone=" + zone_.name +
                         L" items=" + std::to_wstring(total) +
                         L" iconOk=" + std::to_wstring(iconOk) +
                         L" wicOk=" + std::to_wstring(wicOk));
            s_paintLogged = true;
        }
    }

    dcTarget_->EndDraw();

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
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

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void ZoneWindow::OnLButtonDown(int x, int y) {
    dragging_ = true;
    draggingItem_ = HitTestItem(x, y);
    // 必须记录“屏幕坐标”作为拖拽起点，否则窗口会按自身位置向下/右偏移
    GetCursorPos(&dragStart_);
    GetWindowRect(hwnd_, &windowStart_);
    SetCapture(hwnd_);

    if (y < 40) {
        if (onCollapseToggle) onCollapseToggle(zone_.id);
    }
}

void ZoneWindow::OnLButtonUp(int x, int y) {
    dragging_ = false;
    resizing_ = false;
    resizeHit_ = 0;
    draggingItem_.clear();
    ReleaseCapture();
}

void ZoneWindow::StartResize(int hitCode) {
    resizing_ = true;
    resizeHit_ = hitCode;
    GetCursorPos(&dragStart_);
    GetWindowRect(hwnd_, &windowStart_);
    SetCapture(hwnd_);
}

void ZoneWindow::OnMouseMove(int x, int y) {
    POINT pt{};
    GetCursorPos(&pt);

    if (resizing_) {
        const int dx = pt.x - dragStart_.x;
        const int dy = pt.y - dragStart_.y;
        const bool left = resizeHit_ == HTLEFT || resizeHit_ == HTTOPLEFT || resizeHit_ == HTBOTTOMLEFT;
        const bool right = resizeHit_ == HTRIGHT || resizeHit_ == HTTOPRIGHT || resizeHit_ == HTBOTTOMRIGHT;
        const bool top = resizeHit_ == HTTOP || resizeHit_ == HTTOPLEFT || resizeHit_ == HTTOPRIGHT;
        const bool bottom = resizeHit_ == HTBOTTOM || resizeHit_ == HTBOTTOMLEFT || resizeHit_ == HTBOTTOMRIGHT;

        RECT r = windowStart_;
        if (left) r.left += dx;
        if (right) r.right += dx;
        if (top) r.top += dy;
        if (bottom) r.bottom += dy;

        const int minW = 120;
        const int minH = 80;
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
        return;
    }

    if (!dragging_) return;

    if (!draggingItem_.empty() && onItemDrag) {
        if (std::abs(pt.x - dragStart_.x) + std::abs(pt.y - dragStart_.y) > 8) {
            onItemDrag(zone_.id, draggingItem_);
            draggingItem_.clear();
            return;
        }
    }

    int dx = pt.x - dragStart_.x;
    int dy = pt.y - dragStart_.y;
    POINT newPos{windowStart_.left + dx, windowStart_.top + dy};
    // WS_CHILD 子窗口的 SetWindowPos 使用父客户区坐标
    HWND parent = GetAncestor(hwnd_, GA_PARENT);
    if (parent && parent != GetDesktopWindow()) {
        ScreenToClient(parent, &newPos);
    }
    SetWindowPos(hwnd_, nullptr,
                 newPos.x, newPos.y,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void ZoneWindow::OnRButtonUp(int x, int y) {
    POINT pt{x, y};
    ClientToScreen(hwnd_, &pt);
    HMENU menu = CreatePopupMenu();
    std::wstring item = HitTestItem(x, y);

    if (y < 40) {
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
    case kZoneRefreshMsg:
        self->OnPaint();
        return 0;
    case WM_LBUTTONDOWN:
        self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        self->OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
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
        if (pt.y < 40 || !self->HitTestItem(pt.x, pt.y).empty()) return HTCLIENT;
        const LONG width = self->GetZone().rect.right - self->GetZone().rect.left;
        const LONG height = self->GetZone().rect.bottom - self->GetZone().rect.top;
        const int edge = 8;
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
        return HTTRANSPARENT;
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
