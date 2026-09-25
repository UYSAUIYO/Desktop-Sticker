#pragma once

// 解码输出尺寸决策（纯函数）。
//
// 为什么需要它：两条解码路径都把解码结果转成 BGRA 交给 D2D 上屏，而这份转换
// （NV12→RGB32 的色彩转换 + 内存搬运）完全按**源分辨率**计价。4K 素材在 1080p
// 桌面上播放时，3840×2160×4 = 33MB/帧，实测每帧约 25ms —— 多出来的像素在 CPU 上
// 白转一遍，交给 D2D 后再缩小一次。所以按显示尺寸解码就行，画面看不出差别。
//
// 规则：等比缩到 maxW×maxH 之内；**只缩不放**（源比目标小就按源输出，放大交给 GPU）。

namespace desktopsticker::wallpaper {

struct ImageSize {
    int width = 0;
    int height = 0;
};

// maxW/maxH 必须同时为正，否则视为"不限制"（只有一个上界时无法等比缩放）；
// srcW/srcH <= 0（尺寸未知）同样按"不限"处理，让调用方退化到源尺寸而不是算出一个非法值。
inline ImageSize decode_target_size(int srcW, int srcH, int maxW, int maxH) {
    if (srcW <= 0 || srcH <= 0) return {};
    if (maxW <= 0 || maxH <= 0) return { srcW, srcH };
    if (srcW <= maxW && srcH <= maxH) return { srcW, srcH };

    // 手写比较而不是 std::min：这里是头文件，会被不定义 NOMINMAX 的工程包含，
    // windows.h 的 min 宏会把 std::min 打散（Tests 工程就是这么炸的）
    const double sx = static_cast<double>(maxW) / srcW;
    const double sy = static_cast<double>(maxH) / srcH;
    const double scale = sx < sy ? sx : sy;

    int w = static_cast<int>(srcW * scale);
    int h = static_cast<int>(srcH * scale);

    // 取偶数：色度子采样按 2×2 处理，奇数宽高会让部分转换器拒绝或产生偏移
    w -= w % 2;
    h -= h % 2;
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;
    return { w, h };
}

} // namespace desktopsticker::wallpaper
