#pragma once

// 把"cover 缩放"表达成**源纹理的 UV 变换**（纯函数）。
//
// CPU 路径（D2D DrawBitmap）是把源画到一个比目标更大的矩形上、超出部分由渲染目标裁掉；
// GPU 路径用的是铺满视口的全屏三角形，所以要反过来：**不动几何，改采样坐标** ——
// 取源图里居中的一块（宽高比 = 目标宽高比），这就是下面这个矩形。

namespace desktopsticker::wallpaper {

struct UvRect {
    double scaleX = 1.0;    // 采样的源区域宽度（UV 比例）
    double scaleY = 1.0;
    double offsetX = 0.0;   // 居中偏移
    double offsetY = 0.0;
};

// 目标宽高比决定裁剪方向：源更宽就裁左右，源更高就裁上下。只缩不放由调用方保证
// （源和目标同宽高比时返回恒等变换，也就是不裁不缩）。
inline UvRect cover_uv_rect(int srcW, int srcH, int dstW, int dstH) {
    UvRect r;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return r;

    const double srcAspect = static_cast<double>(srcW) / srcH;
    const double dstAspect = static_cast<double>(dstW) / dstH;

    if (srcAspect > dstAspect) {
        // 源更宽：保留中间一条，宽度按目标宽高比收缩
        r.scaleX = dstAspect / srcAspect;
        r.scaleY = 1.0;
        r.offsetX = (1.0 - r.scaleX) * 0.5;
        r.offsetY = 0.0;
    } else if (srcAspect < dstAspect) {
        // 源更高：保留中间一条
        r.scaleX = 1.0;
        r.scaleY = srcAspect / dstAspect;
        r.offsetX = 0.0;
        r.offsetY = (1.0 - r.scaleY) * 0.5;
    }
    // 宽高比相同 → 恒等
    return r;
}

} // namespace desktopsticker::wallpaper
