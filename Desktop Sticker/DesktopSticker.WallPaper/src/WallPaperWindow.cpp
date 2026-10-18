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
    if (!parent_) {
        wp_log("wallpaper WorkerW unavailable; refusing fullscreen top-level fallback");
        Destroy();
        return false;
    }

    // SetParent 要求窗口使用 WS_CHILD；保留其余样式，避免它继续作为全屏弹出窗口显示。
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtrW(
        hwnd_, GWL_STYLE, (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_CHILD);
    if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        wp_log("converting wallpaper window to WS_CHILD failed: " +
               std::to_string(GetLastError()));
        Destroy();
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    SetParent(hwnd_, parent_);
    const DWORD parentError = GetLastError();
    if (GetAncestor(hwnd_, GA_PARENT) != parent_) {
        wp_log("SetParent to wallpaper WorkerW failed: " + std::to_string(parentError));
        Destroy();
        return false;
    }
    embedded_ = true;
    // 与 Wallpaper Engine 一致：认领宿主（样式 + 属性标记），让桌面层确认归属
    DesktopHost::ClaimWorkerW(parent_);

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
