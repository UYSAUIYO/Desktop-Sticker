#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/PresentationArbiter.h>

using namespace desktopsticker::wallpaper;

TEST(Arbiter_StartsAsFrameProducer) {
    PresentationArbiter a;
    ASSERT_TRUE(a.Current() == Presentation::FrameProducer);
}

TEST(Arbiter_OpenSelfPresentingSwitches) {
    PresentationArbiter a;
    const auto d = a.OnBackendOpen(true, true);
    ASSERT_TRUE(d.change);
    ASSERT_TRUE(d.target == Presentation::SelfPresenting);
    ASSERT_TRUE(a.Current() == Presentation::SelfPresenting);
}

TEST(Arbiter_FailedSelfPresentingOpenKeepsCurrent) {
    // 关键：失败的尝试绝不能把正在工作的画面搞黑
    PresentationArbiter a;
    const auto d = a.OnBackendOpen(true, false);
    ASSERT_FALSE(d.change);
    ASSERT_TRUE(a.Current() == Presentation::FrameProducer);
}

TEST(Arbiter_ReturnToFrameProducerFromSelfPresenting) {
    PresentationArbiter a;
    a.OnBackendOpen(true, true);
    const auto d = a.OnBackendOpen(false, true);
    ASSERT_TRUE(d.change);
    ASSERT_TRUE(d.target == Presentation::FrameProducer);
}

TEST(Arbiter_SameStateIsIdempotent) {
    PresentationArbiter a;
    ASSERT_FALSE(a.OnBackendOpen(false, true).change);
    a.OnBackendOpen(true, true);
    ASSERT_FALSE(a.OnBackendOpen(true, true).change);
}

TEST(Arbiter_CloseAlwaysReturnsToFrameProducer) {
    PresentationArbiter a;
    a.OnBackendOpen(true, true);
    const auto d = a.OnBackendClosed();
    ASSERT_TRUE(d.change);
    ASSERT_TRUE(a.Current() == Presentation::FrameProducer);
}

TEST(Arbiter_CloseWhenAlreadyFrameProducerIsNoop) {
    PresentationArbiter a;
    ASSERT_FALSE(a.OnBackendClosed().change);
}

TEST(Arbiter_RepeatedSwitchingStaysConsistent) {
    PresentationArbiter a;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(a.OnBackendOpen(true, true).change);
        ASSERT_TRUE(a.OnBackendOpen(false, true).change);
    }
    ASSERT_TRUE(a.Current() == Presentation::FrameProducer);
}

TEST(Arbiter_FailedOpenAfterSelfPresentingLeavesSelfPresenting) {
    // 自呈现型正在工作时，一个失败的产帧型 Open 不该改变状态
    PresentationArbiter a;
    a.OnBackendOpen(true, true);
    const auto d = a.OnBackendOpen(false, false);
    ASSERT_FALSE(d.change);
    ASSERT_TRUE(a.Current() == Presentation::SelfPresenting);
}
