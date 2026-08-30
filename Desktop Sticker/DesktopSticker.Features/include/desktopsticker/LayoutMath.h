#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <vector>
#include <windows.h>

namespace desktopsticker {

struct QuadColumnParams {
    int screenW = 0;
    int screenH = 0;
    int marginX = 24;  // 贴边留白
    int marginY = 24;
    int gapX = 16;     // 水平卡片间距
    int gapY = 20;     // 垂直卡片间距
    int cardW = 320;
    int cardH = 192;   // 紧凑高度：标题带 + 两行磁贴 + 第三行露头，超出在卡片内滚动
};

// 初始四列布局纯函数：左右各两列贴边、卡片固定紧凑高度、中间大面积留给壁纸。
// 分配规则（左右镜像对称，标准 10 分区时）：
//   左贴边列 = 前半区前 4 张，左内列 = 前半区其余；
//   右贴边列 = 后半区前 4 张，右内列 = 后半区其余（与左侧镜像）。
// 屏幕太窄放不下四列时退化为左右两列贴边。调用方负责写回 zones。
inline std::vector<RECT> ComputeQuadColumnRects(int count, const QuadColumnParams& p) {
    std::vector<RECT> rects;
    if (count <= 0 || p.screenW <= 0 || p.screenH <= 0) return rects;
    rects.resize(static_cast<size_t>(count));

    const int half = (count + 1) / 2;
    const int xLEdge = p.marginX;
    const int xLInner = p.marginX + p.cardW + p.gapX;
    const int xREdge = p.screenW - p.marginX - p.cardW;
    const int xRInner = xREdge - p.gapX - p.cardW;
    const bool wideEnough = xRInner > xLInner + p.cardW; // 四列互不重叠才启用内列

    std::vector<int> colLEdge, colLInner, colRInner, colREdge;
    for (int i = 0; i < half; ++i) {
        if (!wideEnough || static_cast<int>(colLEdge.size()) < 4) colLEdge.push_back(i);
        else colLInner.push_back(i);
    }
    // 右半区与左侧镜像：贴边列先放满 4 张，其余进内列
    for (int i = half; i < count; ++i) {
        if (!wideEnough || static_cast<int>(colREdge.size()) < 4) colREdge.push_back(i);
        else colRInner.push_back(i);
    }

    auto place = [&](const std::vector<int>& idxs, int x) {
        int y = p.marginY;
        for (int idx : idxs) {
            rects[static_cast<size_t>(idx)] = RECT{x, y, x + p.cardW, y + p.cardH};
            y += p.cardH + p.gapY;
        }
    };
    place(colLEdge, xLEdge);
    place(colLInner, xLInner);
    place(colRInner, xRInner);
    place(colREdge, xREdge);
    return rects;
}

} // namespace desktopsticker
