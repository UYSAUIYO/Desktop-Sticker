#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/PausePolicy.h>

using namespace desktopsticker::wallpaper;

TEST(PausePolicy_AllClear_Plays) {
    auto d = reduce_pause_policy(PauseInputs{});
    ASSERT_TRUE(d.state == PlaybackState::Playing);
    ASSERT_TRUE(d.reason == PauseReason::None);
}

TEST(PausePolicy_SessionLockedBeatsEverything) {
    PauseInputs in{};
    in.sessionLocked = true;
    in.displayOff = true;
    in.userPaused = true;
    in.fullscreenCovered = true;
    auto d = reduce_pause_policy(in);
    ASSERT_TRUE(d.state == PlaybackState::Paused);
    ASSERT_TRUE(d.reason == PauseReason::SessionLocked);
}

TEST(PausePolicy_DisplayOffBeatsUserPauseAndFullscreen) {
    PauseInputs in{};
    in.displayOff = true;
    in.userPaused = true;
    in.fullscreenCovered = true;
    ASSERT_TRUE(reduce_pause_policy(in).reason == PauseReason::DisplayOff);
}

TEST(PausePolicy_UserPauseBeatsFullscreen) {
    PauseInputs in{};
    in.userPaused = true;
    in.fullscreenCovered = true;
    ASSERT_TRUE(reduce_pause_policy(in).reason == PauseReason::UserPaused);
}

TEST(PausePolicy_FullscreenAlonePauses) {
    PauseInputs in{};
    in.fullscreenCovered = true;
    auto d = reduce_pause_policy(in);
    ASSERT_TRUE(d.state == PlaybackState::Paused);
    ASSERT_TRUE(d.reason == PauseReason::FullscreenCovered);
}
