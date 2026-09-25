#pragma once

#include <windows.h>

namespace desktopsticker::wallpaper {

// 壁纸宿主 WorkerW 的发现逻辑。
// 自包含实现（不 include Features 的任何头）：Features 里那段 DesktopShellIntegration
// 是 load-bearing 的历史代码，反复回归过，这里刻意不复用、也不改动它。
class DesktopHost {
public:
    // 返回承载壁纸的 WorkerW（即持有 SHELLDLL_DefView 的那个 WorkerW 之后的兄弟窗口）。
    // 找不到（Explorer 结构异常等）返回 nullptr，调用方据此降级为置底普通窗口。
    static HWND FindWallpaperWorkerW();
};

} // namespace desktopsticker::wallpaper
