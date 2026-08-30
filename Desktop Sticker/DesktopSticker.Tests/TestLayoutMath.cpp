#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/LayoutMath.h>

using namespace desktopsticker;

TEST(SideColumn_FourCards_TwoPerColumn) {
    const SideColumnParams p{1920, 1080};
    const auto rects = ComputeSideColumnRects(4, p);
    ASSERT_EQ(4u, rects.size());

    int leftCount = 0, rightCount = 0;
    for (const auto& r : rects) {
        if (r.left == p.margin) ++leftCount;
        else ++rightCount;
    }
    ASSERT_EQ(2, leftCount);
    ASSERT_EQ(2, rightCount);

    // 右列贴右缘，两列等高卡片
    const int cardH = rects[0].bottom - rects[0].top;
    for (const auto& r : rects) {
        if (r.left != p.margin) ASSERT_EQ(1920 - p.margin - p.cardW, r.left);
        ASSERT_EQ(p.cardW, r.right - r.left);
        ASSERT_EQ(cardH, r.bottom - r.top);
    }

    // 卡片高度按可用高度均分，且沿垂直中线居中
    ASSERT_EQ((1080 - 2 * p.margin - p.gap) / 2, cardH);
    const int total = 2 * cardH + p.gap;
    ASSERT_EQ((1080 - total) / 2, rects[0].top);
    ASSERT_EQ((1080 - total) / 2, rects[2].top);
    ASSERT_EQ(rects[0].bottom + p.gap, rects[1].top);
}

TEST(SideColumn_OddCount_ExtraOnLeft) {
    const auto rects = ComputeSideColumnRects(5, SideColumnParams{1920, 1080});
    ASSERT_EQ(5u, rects.size());
    int left = 0, right = 0;
    for (const auto& r : rects) (r.left == 24 ? ++left : ++right);
    ASSERT_EQ(3, left);
    ASSERT_EQ(2, right);
}

TEST(SideColumn_TinyScreen_ClampsCardHeight) {
    const auto rects = ComputeSideColumnRects(6, SideColumnParams{800, 400});
    ASSERT_EQ(6u, rects.size());
    for (const auto& r : rects) ASSERT_TRUE(r.bottom - r.top >= 150);
}

TEST(SideColumn_EmptyAndInvalid) {
    ASSERT_TRUE(ComputeSideColumnRects(0, SideColumnParams{1920, 1080}).empty());
    ASSERT_TRUE(ComputeSideColumnRects(3, SideColumnParams{0, 1080}).empty());
}
