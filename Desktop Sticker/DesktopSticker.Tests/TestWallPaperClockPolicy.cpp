#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/ClockPolicy.h>

using namespace desktopsticker::wallpaper;

TEST(ClockPolicy_AudioMasterWhenAudioActive) {
    ClockInputs in;
    in.hasAudioSource = true;
    in.audioMuted = false;
    in.audioClockValid = true;
    ASSERT_TRUE(choose_clock_master(in) == ClockMaster::Audio);
}

TEST(ClockPolicy_QpcWhenMuted) {
    ClockInputs in;
    in.hasAudioSource = true;
    in.audioMuted = true;          // 用户关掉音频 → 不该再受音频时钟牵制
    in.audioClockValid = true;
    ASSERT_TRUE(choose_clock_master(in) == ClockMaster::Qpc);
}

TEST(ClockPolicy_QpcWhenNoAudioSource) {
    ClockInputs in;
    in.hasAudioSource = false;
    in.audioClockValid = true;
    ASSERT_TRUE(choose_clock_master(in) == ClockMaster::Qpc);
}

TEST(ClockPolicy_QpcWhenClockInvalid) {
    // 设备刚开、还没产出有效时钟 → 先用 QPC，避免视频等一个不存在的时钟
    ClockInputs in;
    in.hasAudioSource = true;
    in.audioClockValid = false;
    ASSERT_TRUE(choose_clock_master(in) == ClockMaster::Qpc);
}

TEST(ClockPolicy_LagIsMasterMinusVideo) {
    ASSERT_EQ(50, video_lag_us(1000, 950));
    ASSERT_EQ(-50, video_lag_us(950, 1000));
}

TEST(ClockPolicy_DropOnlyWhenAudioMasterAndBehind) {
    ASSERT_TRUE(should_drop_to_catch_up(ClockMaster::Audio, 100000));
    ASSERT_FALSE(should_drop_to_catch_up(ClockMaster::Audio, 10000));
    // 音频为主时**不**因视频超前而等待（那会让声音卡顿）
    ASSERT_FALSE(should_drop_to_catch_up(ClockMaster::Audio, -100000));
    // 无音频时由 QPC 调度接管，不做音频式丢帧
    ASSERT_FALSE(should_drop_to_catch_up(ClockMaster::Qpc, 100000));
}

TEST(ClockPolicy_ThresholdIsConfigurable) {
    ASSERT_FALSE(should_drop_to_catch_up(ClockMaster::Audio, 50000, 100000));
    ASSERT_TRUE(should_drop_to_catch_up(ClockMaster::Audio, 150000, 100000));
}
