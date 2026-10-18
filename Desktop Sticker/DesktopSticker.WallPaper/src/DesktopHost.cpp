#include "pch.h"
#include "DesktopHost.h"

#include "Log.h"

namespace desktopsticker::wallpaper {

namespace {

// 未文档化：发给 Progman 让 Explorer 在图标宿主后面再生成一个 WorkerW。
// 与 Wallpaper Engine 2.8 反编译结果一致的参数语义见 FindWallpaperWorkerW 的注释。
constexpr UINT kSpawnWorkerWMessage = 0x052C;

// Wallpaper Engine 的取值：前 10 轮发 0，之后改发 0xD 并带上 lParam=1。
constexpr int kAdvancedParamAfter = 9;
// 全程最多尝试轮数（WE: 0x27）与两档等待。
constexpr int kMaxAttempts = 0x27;
constexpr DWORD kFastRetryMs = 50;
constexpr DWORD kSlowRetryMs = 300;

// 壁纸宿主必须整屏：WE 用 rect 高度 > 0x100 排除掉那些小尺寸的 WorkerW。
constexpr LONG kMinWorkerHeight = 0x100;

// 我们认领 WorkerW 的标记属性（WE 用 "WallpaperEngineParent"；用自家的名字避免互相干扰）。
constexpr const wchar_t* kParentProp = L"DesktopSticker.WallpaperParent";

// WE 给壁纸宿主 WorkerW 设的扩展样式（反编译直译）：
// WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT
constexpr LONG kWorkerExStyle = 0x080000A0;

struct WorkerSearch {
    HWND worker = nullptr;    // 承载壁纸的 WorkerW（DefView 宿主之后的兄弟）
    HWND defViewHost = nullptr; // 持有 SHELLDLL_DefView 的 WorkerW（图标层）
    HWND listView = nullptr;  // SysListView32（图标列表）
};

// 遍历桌面顶层窗口：找到持有 SHELLDLL_DefView 的 WorkerW，取它**后面**的兄弟 WorkerW
// 作为壁纸宿主；并用 GetWindowRect 校验它确实是整屏那块。
BOOL CALLBACK find_defview_worker(HWND top, LPARAM param) {
    auto* out = reinterpret_cast<WorkerSearch*>(param);
    if (!out) return TRUE;

    const HWND defView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!defView) return TRUE;

    const HWND worker = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    if (!worker) return TRUE;

    RECT rc{};
    if (!GetWindowRect(worker, &rc)) return TRUE;
    if (rc.bottom - rc.top <= kMinWorkerHeight) return TRUE;

    out->worker = worker;
    out->defViewHost = top;
    out->listView = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
    return FALSE; // 找到即停
}

WorkerSearch search_wallpaper_worker() {
    WorkerSearch found;
    EnumWindows(find_defview_worker, reinterpret_cast<LPARAM>(&found));
    return found;
}

} // namespace

HWND DesktopHost::FindWallpaperWorkerW() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        wp_log("Progman not found");
        return nullptr;
    }

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        // WE 的发送参数：前 10 轮 wParam=0 / lParam=0；之后 wParam=0xD / lParam=1。
        // 消息语义未公开，这里按反编译结果原样复刻。
        const bool advanced = attempt > kAdvancedParamAfter;
        const WPARAM wparam = advanced ? 0xD : 0;
        const LPARAM lparam = advanced ? 1 : 0;

        DWORD_PTR result = 0;
        SendMessageTimeoutW(progman, kSpawnWorkerWMessage, wparam, lparam, SMTO_NORMAL, 2500,
                            &result);

        const WorkerSearch found = search_wallpaper_worker();
        if (found.worker) {
            if (attempt > 0) {
                wp_log("wallpaper WorkerW found after " + std::to_string(attempt + 1) +
                       " attempt(s)");
            }
            return found.worker;
        }

        // 兼容旧结构：Progman 自己持有 SHELLDLL_DefView，此时没有独立的壁纸宿主。
        if (attempt == 0 && FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
            wp_log("no separate wallpaper WorkerW; Progman holds DefView");
            return nullptr;
        }

        // 长时间拿不到：按 WE 的做法让桌面重建一次（SPI_SETCLIENTAREAANIMATION +
        // SENDCHANGE 会广播设置变更，Explorer 会重新整理桌面窗口树）。
        if (attempt > 0x13) {
            SystemParametersInfoW(0x1043 /* SPI_SETCLIENTAREAANIMATION */, 0,
                                  reinterpret_cast<PVOID>(1),
                                  0x3 /* SPIF_UPDATEINIFILE | SPIF_SENDCHANGE */);
        }
        Sleep(advanced ? kSlowRetryMs : kFastRetryMs);
    }

    // 最后退一步：Progman 下的 WorkerW（旧结构）或任意不含 DefView 的顶层 WorkerW。
    if (HWND worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr)) {
        wp_log("fallback: using Progman-owned WorkerW");
        return worker;
    }
    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
        if (!FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr)) {
            wp_log("fallback: using first WorkerW without DefView");
            return worker;
        }
    }

    wp_log("no wallpaper WorkerW found after " + std::to_string(kMaxAttempts) + " attempts");
    return nullptr;
}

bool DesktopHost::ClaimWorkerW(HWND worker) {
    if (!worker || !IsWindow(worker)) return false;

    // 与 WE 一致：把壁纸宿主的扩展样式钉成 NOACTIVATE|TOOLWINDOW|TRANSPARENT，
    // 让它不抢焦点、不参与任务栏/Alt+Tab、不拦截点击，最后由我们自己的子窗口绘制。
    const LONG previous = GetWindowLongW(worker, GWL_EXSTYLE);
    SetWindowLongW(worker, GWL_EXSTYLE, kWorkerExStyle);
    SetPropW(worker, kParentProp, reinterpret_cast<HANDLE>(1));
    ShowWindow(worker, SW_SHOWNOACTIVATE);

    wp_log("wallpaper WorkerW claimed (exstyle " + std::to_string(previous) + " -> " +
           std::to_string(kWorkerExStyle) + ")");
    return true;
}

bool DesktopHost::IsWallpaperWorkerW(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    return GetPropW(hwnd, kParentProp) == reinterpret_cast<HANDLE>(1);
}

} // namespace desktopsticker::wallpaper
