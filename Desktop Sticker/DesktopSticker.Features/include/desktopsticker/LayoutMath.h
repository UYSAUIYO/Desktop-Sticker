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
    int workTop = 0;    // 工作区顶部（排除任务栏前的工作区，通常为 0）
    int workBottom = 0; // 工作区底部（任务栏上沿）——列底对齐到这里，不留大空隙
    int marginX = 24;   // 贴边留白
    int marginY = 24;   // 工作区上下留白
    int gapX = 16;      // 水平卡片间距
    int gapY = 20;      // 垂直卡片间距
    int cardW = 320;
    int minCardH = 150; // 过小屏不无限压缩
    int maxPerCol = 4;  // 每列卡片数（设置页可选 4 或 5）
};

// 初始四列布局纯函数：左右各两列贴边、卡片高度动态计算、中间大面积留给壁纸。
// 卡片高度按"最满的一列"把工作区高度均分（减去卡片间距），最满的列底对齐任务栏上沿；
// 余量列（如只分到一张的列）保持同一高度、顶部对齐，不单独拉长。
// 每列卡片数由 maxPerCol 控制（设置页可选 4 或 5）。
// 分配规则（左右镜像对称）：贴边列先放满 maxPerCol 张，余量进内列；前半区进左、后半区进右。
// 屏幕太窄放不下四列时退化为左右两列贴边。调用方负责写回 zones。
inline std::vector<RECT> ComputeQuadColumnRects(int count, const QuadColumnParams& p) {
    std::vector<RECT> rects;
    if (count <= 0 || p.screenW <= 0 || p.workBottom <= p.workTop) return rects;
    rects.resize(static_cast<size_t>(count));

    const int availH = p.workBottom - p.workTop - 2 * p.marginY;

    const int half = (count + 1) / 2;
    const int perCol = (std::max)(1, p.maxPerCol);
    const int xLEdge = p.marginX;
    const int xLInner = p.marginX + p.cardW + p.gapX;
    const int xREdge = p.screenW - p.marginX - p.cardW;
    const int xRInner = xREdge - p.gapX - p.cardW;
    const bool wideEnough = xRInner > xLInner + p.cardW; // 四列互不重叠才启用内列

    std::vector<int> colLEdge, colLInner, colRInner, colREdge;
    for (int i = 0; i < half; ++i) {
        if (!wideEnough || static_cast<int>(colLEdge.size()) < perCol) colLEdge.push_back(i);
        else colLInner.push_back(i);
    }
    // 右半区与左侧镜像：贴边列先放满，其余进内列
    for (int i = half; i < count; ++i) {
        if (!wideEnough || static_cast<int>(colREdge.size()) < perCol) colREdge.push_back(i);
        else colRInner.push_back(i);
    }

    // 卡片高度按最满的一列均分工作区高度：该列铺满到任务栏上沿；
    // 其余列（含只分到一张的余量列）沿用同一高度，顶部对齐，不单独拉长
    size_t maxK = 0;
    for (const auto* col : {&colLEdge, &colLInner, &colRInner, &colREdge}) {
        maxK = (std::max)(maxK, col->size());
    }
    int cardH = 0;
    if (maxK > 0) {
        cardH = (availH - static_cast<int>(maxK - 1) * p.gapY) / static_cast<int>(maxK);
        cardH = (std::max)(cardH, p.minCardH); // (std::max) 加括号：免疫 windows.h 的 max 宏
    }

    auto place = [&](const std::vector<int>& idxs, int x) {
        int y = p.workTop + p.marginY;
        for (int idx : idxs) {
            rects[static_cast<size_t>(idx)] = RECT{x, y, x + p.cardW, y + cardH};
            y += cardH + p.gapY;
        }
    };
    place(colLEdge, xLEdge);
    place(colLInner, xLInner);
    place(colRInner, xRInner);
    place(colREdge, xREdge);
    return rects;
}

} // namespace desktopsticker
