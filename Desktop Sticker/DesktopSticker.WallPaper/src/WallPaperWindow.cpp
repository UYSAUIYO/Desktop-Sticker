#include "pch.h"
#include "WallPaperWindow.h"

#include "DesktopHost.h"
#include "Log.h"

namespace desktopsticker::wallpaper {

namespace {

constexpr const wchar_t* kWindowClass = L"DesktopSticker.WallPaper.Window";
constexpr const wchar_t* kWindowTitle = L"Desktop Sticker WallPaper";

LRESULT CALLBACK wall_paper_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        // 不抢焦点、不接管点击
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1; // DComp 负责合成，无需 GDI 擦背景
        case WM_SETCURSOR:
            return TRUE;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ensure_window_class() {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wall_paper_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = nullptr;   // 壁纸不该显示鼠标指针变化
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc)) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            wp_log("RegisterClassExW failed: " + std::to_string(err));
            return false;
        }
    }
    registered = true;
    return true;
}

} // namespace

WallPaperWindow::~WallPaperWindow() {
    Destroy();
}

bool WallPaperWindow::IsWallPaperWindow(HWND hwnd) {
    wchar_t cls[128]{};
    if (GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) == 0) return false;
    return wcscmp(cls, kWindowClass) == 0;
}

void WallPaperWindow::ResizeToPrimaryMonitor() {
    width_ = GetSystemMetrics(SM_CXSCREEN);
    height_ = GetSystemMetrics(SM_CYSCREEN);
    if (width_ <= 0) width_ = 1920;
    if (height_ <= 0) height_ = 1080;
}

bool WallPaperWindow::Create() {
    if (hwnd_) return true;
    if (!ensure_window_class()) return false;

    ResizeToPrimaryMonitor();

    // 不加 WS_EX_TRANSPARENT：透明命中测试在分区窗口上反复导致点击失效，这里同样避开
    hwnd_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kWindowClass, kWindowTitle,
        WS_POPUP,
        0, 0, width_, height_,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        wp_log("CreateWindowExW failed: " + std::to_string(GetLastError()));
        return false;
    }

    parent_ = DesktopHost::FindWallpaperWorkerW();
    if (parent_) {
        const HWND previous = SetParent(hwnd_, parent_);
        if (GetAncestor(hwnd_, GA_PARENT) == parent_) {
            embedded_ = true;
        } else {
            // SetParent 没生效，回滚成独立窗口，走降级路径
            if (previous) SetParent(hwnd_, previous);
            parent_ = nullptr;
            wp_log("SetParent to wallpaper WorkerW did not take effect; degrading");
        }
    } else {
        wp_log("wallpaper WorkerW unavailable; degrading to bottom-most window");
    }

    PlaceAtBottom();

    if (!ShowWindow(hwnd_, SW_SHOWNOACTIVATE)) {
        wp_log("ShowWindow failed: " + std::to_string(GetLastError()));
    }
    UpdateWindow(hwnd_);
    return true;
}

bool WallPaperWindow::PlaceAtBottom() {
    if (!hwnd_) return false;
    // 置底：分区卡片用 HWND_TOP/HWND_BOTTOM 控制层级，壁纸恒在最底
    return SetWindowPos(hwnd_, HWND_BOTTOM, 0, 0, width_, height_,
                        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE) != FALSE;
}

void WallPaperWindow::Destroy() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_HIDE);
    // 先脱离父窗口，避免父窗口销毁时连带销毁过程中的竞态
    SetParent(hwnd_, nullptr);
    if (!DestroyWindow(hwnd_)) {
        wp_log("DestroyWindow failed: " + std::to_string(GetLastError()));
    }
    hwnd_ = nullptr;
    parent_ = nullptr;
    embedded_ = false;
}

} // namespace desktopsticker::wallpaper
