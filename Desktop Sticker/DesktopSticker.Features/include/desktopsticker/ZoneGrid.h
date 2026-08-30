#pragma once
#include <algorithm>
#include <cstddef>

namespace desktopsticker {
namespace zoneui {

// 磁贴卡片的几何常量与网格计算。
// 渲染（RenderContentCache/OnPaint）、命中（HitTestItem）、滚动（OnMouseWheel）
// 共用同一份实现——改布局只动这里，杜绝"显示与点击错位"回归。
inline constexpr float kTitleBand = 48.0f;     // 标题带高度（内容区从 y=48 开始）
inline constexpr int   kTitleHit = 40;         // 标题点击判定高度（拖动/菜单与磁贴区的分界）
inline constexpr float kGridLeft = 16.0f;      // 网格左缘
inline constexpr float kIconSize = 32.0f;      // 磁贴图标边长（也是点击判定边长）
inline constexpr float kLabelTop = 34.0f;      // 名称相对磁贴顶部的偏移
inline constexpr float kLabelWidth = 44.0f;    // 名称排版宽度（窄于列距，居中放置）
inline constexpr float kRowTextBottom = 67.0f; // 单行内容总高（名称底部）
inline constexpr int   kScrollBottomGap = 8;   // 滚到底时末行名称与卡片底边的空隙
inline constexpr int   kScrollStep = 40;       // 每格滚动的像素数
inline constexpr float kScrollEase = 0.45f;    // 每帧向目标位置的趋近比例
inline constexpr int   kResizeEdge = 8;        // 边缘缩放热区宽度
inline constexpr int   kMinCardW = 120;
inline constexpr int   kMinCardH = 80;
inline constexpr float kCornerRadius = 8.0f;

// 每行可容纳的磁贴数：与"放不下即换行、至少 1 列"的网格规则一致
inline int ColumnsForWidth(float width, int columnSpacing) {
    const float usable = width - kGridLeft;
    const int cols = (columnSpacing > 0 && usable > 0)
                         ? static_cast<int>(usable / columnSpacing) : 0;
    return std::max(1, cols);
}

inline int RowCountFor(size_t itemCount, int columns) {
    if (columns <= 0) columns = 1;
    return static_cast<int>((itemCount + static_cast<size_t>(columns) - 1) / columns);
}

// 内容层总高（按行数）；无内容时返回 1，避免空位图
inline int ContentHeightFor(int rows, int rowSpacing) {
    if (rows <= 0) return 1;
    return static_cast<int>(kRowTextBottom) + (rows - 1) * rowSpacing;
}

// 滚动上限：滚到底时末行名称底部与卡片底边之间留 kScrollBottomGap
inline int MaxScrollFor(float cardHeight, int rows, int rowSpacing) {
    const int lastRowTextBottom = (rows - 1) * rowSpacing + static_cast<int>(kRowTextBottom);
    return std::max(0, lastRowTextBottom + kScrollBottomGap -
                           (static_cast<int>(cardHeight) - static_cast<int>(kTitleBand)));
}

// 第 col 列磁贴的 x（内容层坐标）
inline float CellX(int col, int columnSpacing) {
    return kGridLeft + static_cast<float>(col) * columnSpacing;
}

// 第 row 行磁贴的 y（内容层坐标）；屏幕 y = kTitleBand - scrollOffset + CellY
inline float CellY(int row, int rowSpacing) {
    return static_cast<float>(row) * rowSpacing;
}

} // namespace zoneui
} // namespace desktopsticker
