#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/FrameAdvance.h>

#include <cmath>

using namespace desktopsticker::wallpaper;

namespace {
// 每个用例独立状态，避免相互影响
FrameAdvance once(double speed, int64_t elapsedMs, int64_t frameMs) {
    FrameAdvanceState st;
    return frame_advance_policy(st, speed, elapsedMs, frameMs);
}
} // namespace

TEST(FrameAdvance_OneXConsumesOneFramePerSourcePeriod) {
    const auto a = once(1.0, 33, 33);
    ASSERT_EQ(1, a.consume);
    ASSERT_TRUE(a.present);
}

TEST(FrameAdvance_HalfFrameHoldsCurrentFrame) {
    const auto a = once(1.0, 16, 33);
    ASSERT_EQ(0, a.consume);
    ASSERT_TRUE(a.present);
}

TEST(FrameAdvance_FourXConsumesFourFrames) {
    ASSERT_EQ(4, once(4.0, 33, 33).consume);
}

TEST(FrameAdvance_QuarterXAdvancesOneEveryFourthPeriod) {
    ASSERT_EQ(0, once(0.25, 33, 33).consume);
    ASSERT_EQ(1, once(0.25, 132, 33).consume);
}

TEST(FrameAdvance_LongStallIsClamped) {
    ASSERT_EQ(8, once(1.0, 100000, 33).consume);
}

TEST(FrameAdvance_InvalidSpeedFallsBackToOne) {
    ASSERT_EQ(1, once(0.0, 33, 33).consume);
    ASSERT_EQ(1, once(-2.0, 33, 33).consume);
    ASSERT_EQ(1, once(std::nan(""), 33, 33).consume);
}

TEST(FrameAdvance_SpeedIsClampedToRange) {
    ASSERT_TRUE(clamp_speed(100.0) == kMaxSpeed);
    ASSERT_TRUE(clamp_speed(0.01) == kMinSpeed);
    ASSERT_TRUE(clamp_speed(1.5) == 1.5);
}

TEST(FrameAdvance_ZeroElapsedDoesNotAdvance) {
    ASSERT_EQ(0, once(1.0, 0, 33).consume);
}

TEST(FrameAdvance_InvalidFrameDurationDoesNotAdvance) {
    ASSERT_EQ(0, once(1.0, 33, 0).consume);
    ASSERT_EQ(0, once(1.0, 33, -5).consume);
}

// ---- 回归：余量必须跨调用累积 ----
// 实测 30fps 素材在"30ms/46ms/62ms 交替"的节拍下只出 9 帧/秒：
// 因为 floor(46/33)=1 丢掉了 0.39 帧的余量，而 30ms 的拍又整拍保持。

TEST(FrameAdvance_JitteryTicksDoNotLoseFrames) {
    FrameAdvanceState st;
    // 模拟实测的抖动序列：一秒钟的节拍（30/31/46/62ms 混合，约 1 秒共 30 个源帧）
    const int64_t ticks[] = { 30, 31, 30, 46, 31, 62, 30, 46, 31, 30,
                              46, 30, 31, 62, 30, 46, 31, 30, 46, 31 };
    int total = 0;
    int64_t elapsedTotal = 0;
    for (int64_t t : ticks) {
        total += frame_advance_policy(st, 1.0, t, 33).consume;
        elapsedTotal += t;
    }
    // 期望消费 ≈ 总时间 / 帧时长；允许 ±1 帧的取整误差
    const double expected = static_cast<double>(elapsedTotal) / 33.0;
    ASSERT_TRUE(static_cast<double>(total) >= expected - 1.0);
    ASSERT_TRUE(static_cast<double>(total) <= expected + 1.0);
}

TEST(FrameAdvance_DebtIsKeptNotDiscarded) {
    FrameAdvanceState st;
    // 前两拍合计 1.9 帧：应消费 1 帧并留下 0.9 的余量
    ASSERT_EQ(1, frame_advance_policy(st, 1.0, 33, 33).consume);
    ASSERT_EQ(0, frame_advance_policy(st, 1.0, 30, 33).consume);   // 余量 0.9，仍不足一帧
    // 再来 30ms：0.9 + 0.91 = 1.81 → 消费 1，留 0.81
    ASSERT_EQ(1, frame_advance_policy(st, 1.0, 30, 33).consume);
}

TEST(FrameAdvance_ResetClearsDebt) {
    FrameAdvanceState st;
    frame_advance_policy(st, 1.0, 30, 33);   // 留下 0.9 余量
    st.Reset();
    ASSERT_EQ(0, frame_advance_policy(st, 1.0, 30, 33).consume);   // 余量已清零，仍不足一帧
}

TEST(FrameAdvance_ManySmallTicksEventuallyAdvance) {
    // 每拍 10ms（10/33 ≈ 0.30 帧）：4 拍后应推进一帧，而不是永远保持
    FrameAdvanceState st;
    int total = 0;
    for (int i = 0; i < 4; ++i) total += frame_advance_policy(st, 1.0, 10, 33).consume;
    ASSERT_EQ(1, total);
}
