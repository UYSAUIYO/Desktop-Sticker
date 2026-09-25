#include "pch.h"
#include "DesktopHost.h"

#include "Log.h"

namespace desktopsticker::wallpaper {

namespace {

constexpr UINT kSpawnWorkerWMessage = 0x052C;

BOOL CALLBACK find_defview_worker(HWND top, LPARAM param) {
    HWND defView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!defView) return TRUE;

    // 持有 DefView 的 WorkerW 的下一个兄弟 WorkerW 才是壁纸宿主
    HWND* out = reinterpret_cast<HWND*>(param);
    *out = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    return FALSE; // 找到即停
}

} // namespace

HWND DesktopHost::FindWallpaperWorkerW() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        wp_log("Progman not found");
        return nullptr;
    }

    // 未文档化消息：促使 Progman 生成 WorkerW（与 Wallpaper Engine 同思路）
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, kSpawnWorkerWMessage, 0, 0, SMTO_NORMAL, 1000, &result);

    HWND wallpaperWorker = nullptr;

    // 主路径：有 WorkerW 持有 SHELLDLL_DefView，其下一个 WorkerW 是壁纸宿主
    EnumWindows(find_defview_worker, reinterpret_cast<LPARAM>(&wallpaperWorker));
    if (wallpaperWorker) return wallpaperWorker;

    // 兼容旧路径：Progman 直接持有 SHELLDLL_DefView，此时无独立壁纸宿主
    if (FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
        wp_log("no separate wallpaper WorkerW; Progman holds DefView");
        return nullptr;
    }

    // 再退一步：枚举到的第一个 WorkerW 作为宿主
    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
        if (!FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr)) {
            return worker;
        }
    }

    wp_log("no wallpaper WorkerW found");
    return nullptr;
}

} // namespace desktopsticker::wallpaper
