#include "pch.h"
#include "ResMonWindow.h"

namespace desktopsticker::resmon {

namespace {

constexpr const wchar_t* kWindowClass = L"DesktopSticker.ResMon.Window";
constexpr const wchar_t* kWindowTitle = L"资源管理器 — Desktop Sticker";
constexpr int kLogicalWidth = 920;
constexpr int kLogicalHeight = 640;

// GetDpiForWindow / AdjustWindowRectExForDpi 是 Win10 1607+ 才有导出；
// 动态解析以免依赖各项目的 WINVER 设置。
UINT dpi_for_window(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static const Fn fn = [] {
        HMODULE u = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return u ? reinterpret_cast<Fn>(GetProcAddress(u, "GetDpiForWindow")) : nullptr;
    }();
    if (fn) {
        const UINT dpi = fn(hwnd);
        if (dpi > 0) return dpi;
    }
    HDC dc = GetDC(nullptr);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96u;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi : 96u;
}

void adjust_window_rect(HWND hwnd, RECT& r) {
    using Fn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static const Fn fn = [] {
        HMODULE u = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return u ? reinterpret_cast<Fn>(GetProcAddress(u, "AdjustWindowRectExForDpi")) : nullptr;
    }();
    if (fn && fn(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_for_window(hwnd))) return;
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
}

bool ensure_class() {    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ResMonWindow::wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    registered = true;
    return true;
}

} // namespace

bool ResMonWindow::IsResMonWindow(HWND hwnd) {
    wchar_t cls[128]{};
    if (GetClassNameW(hwnd, cls, 128) > 0) {
        return wcscmp(cls, kWindowClass) == 0;
    }
    return false;
}

ResMonWindow::~ResMonWindow() {
    Destroy();
}

bool ResMonWindow::Create() {
    if (hwnd_) return true;
    if (!ensure_class()) return false;

    const UINT dpi = dpi_for_window(nullptr);
    const int w = MulDiv(kLogicalWidth, static_cast<int>(dpi), 96);
    const int h = MulDiv(kLogicalHeight, static_cast<int>(dpi), 96);
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    RECT r{ 0, 0, w, h };
    // 按 DPI 换算外框，保证客户区接近逻辑尺寸
    adjust_window_rect(nullptr, r);
    const int outerW = r.right - r.left;
    const int outerH = r.bottom - r.top;
    const int x = (screenW - outerW) / 2;
    const int y = (screenH - outerH) / 2;

    hwnd_ = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                            x > 0 ? x : CW_USEDEFAULT, y > 0 ? y : CW_USEDEFAULT,
                            outerW, outerH, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    notify_size();
    return true;
}

void ResMonWindow::Destroy() {
    if (!hwnd_) return;
    onDestroy_(); // 先让宿主释放 WebView2 控制器，再销毁窗口
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    clientW_ = clientH_ = 0;
}

void ResMonWindow::ShowAndFocus() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void ResMonWindow::notify_size() {
    if (!hwnd_) return;
    RECT rc{};
    if (GetClientRect(hwnd_, &rc)) {
        clientW_ = rc.right - rc.left;
        clientH_ = rc.bottom - rc.top;
    }
    if (onSize_) onSize_(clientW_, clientH_);
}

LRESULT CALLBACK ResMonWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ResMonWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ResMonWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ResMonWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
        case WM_SIZE:
            if (self) self->notify_size();
            return 0;
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // WebView2 负责绘制
        case WM_CLOSE:
            DestroyWindow(hwnd); // 销毁而非隐藏
            return 0;
        case WM_DESTROY: {
            if (self) self->hwnd_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            break;
    }

    if (msg >= WM_APP && self && self->onAppMessage_) {
        if (self->onAppMessage_(msg, wp, lp)) return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace desktopsticker::resmon
