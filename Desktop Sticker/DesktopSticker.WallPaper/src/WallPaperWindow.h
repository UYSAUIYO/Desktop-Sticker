#pragma once

#include <cstdint>
#include <string>

namespace desktopsticker::wallpaper {

// 壁纸窗口：桌面层子窗口，位于原生桌面图标与分区卡片之下。
// 桌面宿主不可用时停止显示，避免全屏普通窗口遮挡桌面。
class WallPaperWindow {
public:
    ~WallPaperWindow();

    // 创建窗口并尝试嵌入桌面；embedded() 表明是否走的是嵌入路径
    bool Create();
    void Destroy();

    bool PlaceAtBottom();               // 分区层级变化后重申底部位置
    void ResizeToPrimaryMonitor();

    HWND Handle() const { return hwnd_; }
    bool Embedded() const { return embedded_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

    // 供"双击桌面空白"检测排除自身：壁纸窗口绝不能被当作遮挡源
    static bool IsWallPaperWindow(HWND hwnd);

private:
    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    bool embedded_ = false;
    int width_ = 0;
    int height_ = 0;
};

} // namespace desktopsticker::wallpaper
