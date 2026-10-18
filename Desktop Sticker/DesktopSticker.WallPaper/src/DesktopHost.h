#pragma once

#include <windows.h>

namespace desktopsticker::wallpaper {

// 壁纸宿主 WorkerW 的发现与认领逻辑。
// 自包含实现（不 include Features 的任何头）：Features 里那段 DesktopShellIntegration
// 是 load-bearing 的历史代码，反复回归过，这里刻意不复用、也不改动它。
//
// 实现依据 Wallpaper Engine wallpaperservice64 / wallpaper64 2.8 的反编译结果：
//   1) FindWindowW("Progman")；
//   2) 反复 SendMessageTimeoutW(Progman, 0x052C, wParam, lParam, ...) 直到
//      Explorer 在图标宿主后面生成 WorkerW（前 10 轮 0/0，之后 0xD/1）；
//   3) 枚举桌面顶层窗口：持有 SHELLDLL_DefView 的 WorkerW 是图标层，取它**后面**
//      的兄弟 WorkerW 作为壁纸宿主，并用 GetWindowRect 高度 > 0x100 校验；
//   4) 认领：把宿主扩展样式钉成 NOACTIVATE|TOOLWINDOW|TRANSPARENT，
//      并用窗口属性打标记（WE 用 "WallpaperEngineParent"）。
class DesktopHost {
public:
    // 返回承载壁纸的 WorkerW（即持有 SHELLDLL_DefView 的那个 WorkerW 之后的兄弟窗口）。
    // 找不到（Explorer 结构异常等）返回 nullptr，调用方据此降级为置底普通窗口。
    static HWND FindWallpaperWorkerW();

    // 认领宿主：设置扩展样式、显示、打属性标记。必须在 SetParent 成功之后调用。
    static bool ClaimWorkerW(HWND worker);

    // 该窗口是不是我们认领过的壁纸宿主（判断依据是属性标记）。
    static bool IsWallpaperWorkerW(HWND hwnd);
};

} // namespace desktopsticker::wallpaper
