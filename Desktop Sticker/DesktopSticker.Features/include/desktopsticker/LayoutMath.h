#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <vector>
#include <windows.h>

namespace desktopsticker {

struct SideColumnParams {
    int screenW = 0;
    int screenH = 0;
    int margin = 24;    // 贴边留白
    int gap = 16;       // 卡片间距
    int cardW = 320;
    int minCardH = 150; // 过小屏不无限压缩
};

// 侧列布局纯函数：卡片只占主屏左右两侧，垂直方向围绕中线均匀分布，左列多放一张。
// 独立成纯函数以便对分布数学做单元测试；调用方负责写回 zones 的 rect/monitorIndex。
inline std::vector<RECT> ComputeSideColumnRects(int count, const SideColumnParams& p) {
    std::vector<RECT> rects;
    if (count <= 0 || p.screenW <= 0 || p.screenH <= 0) return rects;
    rects.resize(static_cast<size_t>(count));

    const int nLeft = (count + 1) / 2; // 左列多放一张
    const int nRight = count - nLeft;
    const int availH = p.screenH - 2 * p.margin;
    int cardH = (availH - (nLeft - 1) * p.gap) / nLeft;
    cardH = (std::max)(cardH, p.minCardH); // (std::max) 加括号：免疫 windows.h 的 max 宏

    auto place = [&](int beginIdx, int n, int x) {
        const int total = n * cardH + (n - 1) * p.gap;
        int y = (p.screenH - total) / 2; // 沿垂直中线居中展开
        for (int i = 0; i < n; ++i) {
            rects[static_cast<size_t>(beginIdx + i)] = RECT{x, y, x + p.cardW, y + cardH};
            y += cardH + p.gap;
        }
    };
    place(0, nLeft, p.margin);
    if (nRight > 0) place(nLeft, nRight, p.screenW - p.margin - p.cardW);
    return rects;
}

} // namespace desktopsticker
