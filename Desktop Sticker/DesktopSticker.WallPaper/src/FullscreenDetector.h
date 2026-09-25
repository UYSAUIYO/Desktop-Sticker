#pragma once

#include <windows.h>

namespace desktopsticker::wallpaper {

// 触发播放/恢复判定的输入来源。
// 判定本身是纯函数（PausePolicy），这里只负责从系统取事实。
struct SystemFacts {
    bool sessionLocked = false;
    bool displayOff = false;
    bool fullscreenCovered = false;
};

class FullscreenDetector {
public:
    // 枚举所有可见、未最小化、未被 DWM 隐藏的顶层窗口，判断是否有窗口覆盖了
    // 壁纸所在显示器的工作区。不只看焦点窗口。
    // exclude: 壁纸窗口自身，以及桌面层窗口都要排除，避免把壁纸当遮挡源。
    static bool AnyFullscreenCovering(HWND exclude);

    static bool SessionLocked();
    static bool DisplayOff();

    static SystemFacts Collect(HWND exclude);
};

} // namespace desktopsticker::wallpaper
