#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/LayoutMath.h>

using namespace desktopsticker;

namespace {
// 1080p 屏、任务栏 48px 的工作区参数
QuadColumnParams DesktopParams() {
    QuadColumnParams p{};
    p.screenW = 1920;
    p.workTop = 0;
    p.workBottom = 1032;
    p.maxPerCol = 4;
    return p;
}
} // namespace

TEST(QuadColumn_TenCards_FillsWorkAreaWithMirroredColumns) {
    const auto p = DesktopParams();
    const auto r = ComputeQuadColumnRects(10, p);
    ASSERT_EQ(10u, r.size());

    // 高度按最满的列（4 张）均分：(984-3*20)/4 = 231，全部卡片同高
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(24, r[i].left);
        ASSERT_EQ(24 + i * (231 + 20), r[i].top);
    }
    ASSERT_EQ(24 + 320 + 16, r[4].left);
    ASSERT_EQ(24, r[4].top);
    ASSERT_EQ(231, r[4].bottom - r[4].top); // 余量列保持正常高度，不拉长
    for (int i = 5; i < 9; ++i) {
        ASSERT_EQ(1920 - 24 - 320, r[i].left);
        ASSERT_EQ(24 + (i - 5) * (231 + 20), r[i].top);
    }
    ASSERT_EQ(1920 - 24 - 320 - 16 - 320, r[9].left);
    ASSERT_EQ(24, r[9].top);
    ASSERT_EQ(231, r[9].bottom - r[9].top);
    // 最满的列底对齐任务栏上沿附近（1008 = 1032 - 24），底部不留大空隙
    ASSERT_EQ(1008, r[3].bottom);
    ASSERT_EQ(1008, r[8].bottom);
    for (const auto& rect : r) ASSERT_EQ(320, rect.right - rect.left);
}

TEST(QuadColumn_FivePerColumn_TwoFullColumns) {
    auto p = DesktopParams();
    p.maxPerCol = 5;
    const auto r = ComputeQuadColumnRects(10, p);
    ASSERT_EQ(10u, r.size());
    // 左右各一列 5 张：高度 (984-4*20)/5 = 180
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(24, r[i].left);
        ASSERT_EQ(24 + i * (180 + 20), r[i].top);
    }
    for (int i = 5; i < 10; ++i) {
        ASSERT_EQ(1920 - 24 - 320, r[i].left);
        ASSERT_EQ(24 + (i - 5) * (180 + 20), r[i].top);
    }
    ASSERT_EQ(1004, r[4].bottom); // 整数截断后距底边留白不足一个间距
}

TEST(QuadColumn_NarrowScreen_DegradesToTwoEdgeColumns) {
    QuadColumnParams p{};
    p.screenW = 900;
    p.workBottom = 1032;
    const auto r = ComputeQuadColumnRects(6, p);
    ASSERT_EQ(6u, r.size());
    for (int i = 0; i < 3; ++i) ASSERT_EQ(24, r[i].left);
    for (int i = 3; i < 6; ++i) ASSERT_EQ(900 - 24 - 320, r[i].left);
    // 高度按列均分：(984-2*20)/3 = 314
    ASSERT_EQ(314, r[0].bottom - r[0].top);
}

TEST(QuadColumn_SingleAndInvalid) {
    const auto r = ComputeQuadColumnRects(1, DesktopParams());
    ASSERT_EQ(1u, r.size());
    ASSERT_EQ(24, r[0].left);
    ASSERT_EQ(24, r[0].top);
    ASSERT_TRUE(ComputeQuadColumnRects(0, DesktopParams()).empty());
    QuadColumnParams bad{};
    bad.screenW = 1920;
    ASSERT_TRUE(ComputeQuadColumnRects(3, bad).empty()); // workBottom 未设置
}
