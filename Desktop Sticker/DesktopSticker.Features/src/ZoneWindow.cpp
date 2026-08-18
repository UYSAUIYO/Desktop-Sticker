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
    // 不使用 WS_EX_LAYERED：分层窗口在跨进程桌面子窗口上可能完全不渲染（全透明）
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    hwnd_ = CreateWindowExW(exStyle, kZoneWindowClass, L"",
                            style,
                            zone_.rect.left, zone_.rect.top,
                            zone_.rect.right - zone_.rect.left,
                            zone_.rect.bottom - zone_.rect.top,
                            nullptr, nullptr, hInst_, this);
    if (!hwnd_) return false;

    g_windows[hwnd_] = this;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    HRGN rgn = CreateRoundRectRgn(0, 0,
                                  zone_.rect.right - zone_.rect.left + 1,
                                  zone_.rect.bottom - zone_.rect.top + 1, 16, 16);
    SetWindowRgn(hwnd_, rgn, TRUE);

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
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
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
            tileY += 48.0f;
        }
    }
    return L"";
}

bool ZoneWindow::EnsureD2DResources() {
    if (target_) return true;
    if (!factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwriteFactory_));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wicFactory_));
        if (!factory_ || !dwriteFactory_ || !wicFactory_) return false;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                     D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)),
        &target_);
    if (!target_) return false;

    target_->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E, 0.80f), &bgBrush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), &titleBrush_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     14.0f, L"zh-cn", &textFormat_);
    return true;
}

void ZoneWindow::ReleaseD2DResources() {
    if (textFormat_) textFormat_->Release();
    if (titleBrush_) titleBrush_->Release();
    if (bgBrush_) bgBrush_->Release();
    if (target_) target_->Release();
    if (dwriteFactory_) dwriteFactory_->Release();
    if (wicFactory_) wicFactory_->Release();
    if (factory_) factory_->Release();
    textFormat_ = nullptr; titleBrush_ = nullptr; bgBrush_ = nullptr;
    target_ = nullptr; dwriteFactory_ = nullptr; wicFactory_ = nullptr; factory_ = nullptr;
}

void ZoneWindow::OnPaint() {
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    if (!EnsureD2DResources()) {
        EndPaint(hwnd_, &ps);
        return;
    }

    target_->BeginDraw();
    // 实色深色背景（非分层窗口 Clear(0,0) 会变黑，这里直接用不透明深色）
    target_->Clear(D2D1::ColorF(0x1E1E1E, 1.0f));
    const float width = static_cast<float>(zone_.rect.right - zone_.rect.left);
    const float height = static_cast<float>(zone_.rect.bottom - zone_.rect.top);
    target_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(1, 1, width - 1, height - 1), 16.0f, 16.0f),
        bgBrush_, 1.0f);

    std::wstring title = zone_.collapsed ? L"\x25B8 " + zone_.name : L"\x25BE " + zone_.name;
    target_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), textFormat_,
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
                            const HRESULT d2dHr = target_->CreateBitmapFromWicBitmap(converter, nullptr, &d2dBmp);
                            if (SUCCEEDED(d2dHr)) {
                                ++wicOk;
                                target_->DrawBitmap(d2dBmp, D2D1::RectF(x, y, x + 32, y + 32),
                                                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                                d2dBmp->Release();
                            } else {
                                static bool s_d2dErrorLogged = false;
                                if (!s_d2dErrorLogged) {
                                    ZoneDebugLog(L"[zone paint] CreateBitmapFromWicBitmap failed hr=0x" +
                                                 std::to_wstring(static_cast<unsigned long>(d2dHr)));
                                    s_d2dErrorLogged = true;
                                }
                            }
                        }
                        converter->Release();
                    }
                    wicBmp->Release();
                } else {
                    static bool s_wicErrorLogged = false;
                    if (!s_wicErrorLogged) {
                        ZoneDebugLog(L"[zone paint] CreateBitmapFromHICON failed");
                        s_wicErrorLogged = true;
                    }
                }
            } else {
                target_->FillRectangle(D2D1::RectF(x, y, x + 32, y + 32), bgBrush_);
            }
            x += 48.0f;
            if (x + 48 > width) {
                x = 16.0f;
                y += 48.0f;
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

    target_->EndDraw();
    EndPaint(hwnd_, &ps);
}

void ZoneWindow::OnLButtonDown(int x, int y) {
    dragging_ = true;
    draggingItem_ = HitTestItem(x, y);
    dragStart_ = POINT{x, y};
    GetWindowRect(hwnd_, &windowStart_);
    SetCapture(hwnd_);

    if (y < 40) {
        if (onCollapseToggle) onCollapseToggle(zone_.id);
    }
}

void ZoneWindow::OnLButtonUp(int x, int y) {
    dragging_ = false;
    draggingItem_.clear();
    ReleaseCapture();
}

void ZoneWindow::OnMouseMove(int x, int y) {
    if (!dragging_) return;
    POINT pt{};
    GetCursorPos(&pt);

    if (!draggingItem_.empty() && onItemDrag) {
        if (std::abs(pt.x - dragStart_.x) + std::abs(pt.y - dragStart_.y) > 8) {
            onItemDrag(zone_.id, draggingItem_);
            draggingItem_.clear();
            return;
        }
    }

    int dx = pt.x - dragStart_.x;
    int dy = pt.y - dragStart_.y;
    SetWindowPos(hwnd_, nullptr,
                 windowStart_.left + dx, windowStart_.top + dy,
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
