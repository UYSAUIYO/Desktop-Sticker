#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/FrameScheduler.h>

using namespace desktopsticker::wallpaper;

TEST(FrameScheduler_AheadOfDeadline_WaitsRemaining) {
    auto d = schedule_frame(1000, 3000);
    ASSERT_FALSE(d.skipBacklog);
    ASSERT_EQ(2000ll, d.waitHundredNs);
}

TEST(FrameScheduler_ExactlyAtDeadline_NoWait) {
    auto d = schedule_frame(3000, 3000);
    ASSERT_FALSE(d.skipBacklog);
    ASSERT_EQ(0ll, d.waitHundredNs);
}

TEST(FrameScheduler_PastDeadline_SkipsBacklog) {
    auto d = schedule_frame(5000, 3000);
    ASSERT_TRUE(d.skipBacklog);
    ASSERT_EQ(0ll, d.waitHundredNs);
}

TEST(FrameScheduler_NextDeadlineAdvances) {
    ASSERT_EQ(9000ll, next_deadline(3000, 6000));
}

TEST(FrameScheduler_NextDeadlineAfterBacklog_DropsMissedFrames) {
    // 当前 20000，帧长 1000，前一期限 3000 → 下一个未错过的期限是 21000
    ASSERT_EQ(21000ll, next_deadline_not_before(3000, 1000, 20000));
}
