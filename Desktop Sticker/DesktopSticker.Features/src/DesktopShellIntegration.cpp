#include "pch.h"
#include "desktopsticker/DesktopShellIntegration.h"

#include <commctrl.h>

namespace desktopsticker {

bool DesktopShellIntegration::Initialize() {
    if (FindDesktopWindows()) return true;
    // 二次尝试：等待 Explorer 就绪后重试一次
    Sleep(500);
    return FindDesktopWindows();
}

void DesktopShellIntegration::Shutdown() {
    wins_ = ShellDesktopWindows{};
}

bool DesktopShellIntegration::FindDesktopWindows() {
    wins_.progman = FindWindowW(L"Progman", nullptr);
    if (!wins_.progman) return false;

    // 未文档化消息：让 Progman 生成 WorkerW
    SendMessageTimeoutW(wins_.progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
        HWND defView = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) {
            wins_.defView = defView;
            wins_.listView = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
            // 下一个 WorkerW 是壁纸宿主
            wins_.workerw = FindWindowExW(nullptr, worker, L"WorkerW", nullptr);
            return wins_.defView != nullptr;
        }
    }

    // 兼容旧路径：Progman 直接持有 SHELLDLL_DefView
    wins_.defView = FindWindowExW(wins_.progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (wins_.defView) {
        wins_.listView = FindWindowExW(wins_.defView, nullptr, L"SysListView32", nullptr);
    }
    return wins_.defView != nullptr;
}

bool DesktopShellIntegration::EmbedWindow(HWND hwnd, bool aboveIcons) {
    if (!wins_.defView) return false;
    HWND parent = aboveIcons ? wins_.defView : wins_.workerw;
    if (!parent) return false;

    SetParent(hwnd, parent);
    SetWindowPos(hwnd, aboveIcons ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

bool DesktopShellIntegration::SubclassListView(SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data) {
    if (!wins_.listView) return false;
    return SetWindowSubclass(wins_.listView, proc, id, data) != FALSE;
}

bool DesktopShellIntegration::UnsubclassListView(SUBCLASSPROC proc, UINT_PTR id) {
    if (!wins_.listView) return false;
    return RemoveWindowSubclass(wins_.listView, proc, id) != FALSE;
}

} // namespace desktopsticker
