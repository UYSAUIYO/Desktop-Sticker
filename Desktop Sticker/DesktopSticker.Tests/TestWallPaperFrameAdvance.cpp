#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/FrameAdvance.h>

#include <cmath>

using namespace desktopsticker::wallpaper;

TEST(FrameAdvance_OneXConsumesOneFramePerSourcePeriod) {
    // 1× 且经过时间正好一帧 → 消费 1
    const auto a = frame_advance_policy(1.0, 33, 33);
    ASSERT_EQ(1, a.consume);
    ASSERT_TRUE(a.present);
}

TEST(FrameAdvance_HalfFrameHoldsCurrentFrame) {
    // 经过时间不足一帧 → 不推进，但保持呈现
    const auto a = frame_advance_policy(1.0, 16, 33);
    ASSERT_EQ(0, a.consume);
    ASSERT_TRUE(a.present);
}

TEST(FrameAdvance_FourXConsumesFourFrames) {
    const auto a = frame_advance_policy(4.0, 33, 33);
    ASSERT_EQ(4, a.consume);
}

TEST(FrameAdvance_QuarterXAdvancesOneEveryFourthPeriod) {
    // 0.25× 且经过一帧时间 → 累积不足一帧 → 保持
    ASSERT_EQ(0, frame_advance_policy(0.25, 33, 33).consume);
    // 经过四帧时间 → 推进一帧
    ASSERT_EQ(1, frame_advance_policy(0.25, 132, 33).consume);
}

TEST(FrameAdvance_LongStallIsClamped) {
    // 长时间停滞后不能一次吞掉半个视频
    const auto a = frame_advance_policy(1.0, 100000, 33);
    ASSERT_EQ(8, a.consume);
}

TEST(FrameAdvance_InvalidSpeedFallsBackToOne) {
    ASSERT_EQ(1, frame_advance_policy(0.0, 33, 33).consume);
    ASSERT_EQ(1, frame_advance_policy(-2.0, 33, 33).consume);
    ASSERT_EQ(1, frame_advance_policy(std::nan(""), 33, 33).consume);
}

TEST(FrameAdvance_SpeedIsClampedToRange) {
    ASSERT_TRUE(clamp_speed(100.0) == kMaxSpeed);
    ASSERT_TRUE(clamp_speed(0.01) == kMinSpeed);
    ASSERT_TRUE(clamp_speed(1.5) == 1.5);
}

TEST(FrameAdvance_ZeroElapsedDoesNotAdvance) {
    ASSERT_EQ(0, frame_advance_policy(1.0, 0, 33).consume);
}

TEST(FrameAdvance_InvalidFrameDurationDoesNotAdvance) {
    ASSERT_EQ(0, frame_advance_policy(1.0, 33, 0).consume);
    ASSERT_EQ(0, frame_advance_policy(1.0, 33, -5).consume);
}
