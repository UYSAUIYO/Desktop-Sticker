#pragma once
#include <windows.h>

#include "desktopsticker/Export.h"

namespace desktopsticker {

struct DESKTOPSTICKER_API ShellDesktopWindows {
    HWND progman = nullptr;
    HWND workerw = nullptr;   // 壁纸宿主 WorkerW
    HWND defView = nullptr;   // SHELLDLL_DefView
    HWND listView = nullptr;  // SysListView32
};

class DESKTOPSTICKER_API DesktopShellIntegration {
public:
    bool Initialize();
    void Shutdown();

    // aboveIcons=true → 挂到 SHELLDLL_DefView 并置于图标之上；false → 挂到壁纸宿主
    bool EmbedWindow(HWND hwnd, bool aboveIcons);

    bool SubclassListView(SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data);
    bool UnsubclassListView(SUBCLASSPROC proc, UINT_PTR id);

    const ShellDesktopWindows& Windows() const { return wins_; }
    bool IsReady() const { return wins_.defView != nullptr; }

private:
    bool FindDesktopWindows();

    ShellDesktopWindows wins_;
};

} // namespace desktopsticker
