#pragma once

// 主时钟选择与漂移补偿（纯函数）。
// 有音频且未静音时以音频时钟为主：视频按音频对齐、落后即丢帧；
// 其余情况沿用 QPC。这个模块只做决策，不碰任何设备。

#include <cstdint>

namespace desktopsticker::wallpaper {

enum class ClockMaster { Qpc, Audio };

struct ClockInputs {
    bool hasAudioSource = false;   // 后端提供了音轨
    bool audioMuted = false;       // 用户关掉了音频
    bool audioClockValid = false;  // 音频引擎已产出有效时钟
};

inline ClockMaster choose_clock_master(const ClockInputs& in) {
    if (in.hasAudioSource && !in.audioMuted && in.audioClockValid) return ClockMaster::Audio;
    return ClockMaster::Qpc;
}

// 视频相对主时钟的落后量（正数 = 视频落后，需要追赶/丢帧）
inline int64_t video_lag_us(int64_t masterUs, int64_t videoUs) {
    return masterUs - videoUs;
}

// 是否应当丢帧追赶：音频为主且落后超过阈值（默认 80ms）时丢，
// 避免 A/V 越走越偏；音频为主时**不**做"视频超前就等待"（那会让声音卡顿）。
inline bool should_drop_to_catch_up(ClockMaster master, int64_t lagUs, int64_t thresholdUs = 80000) {
    if (master != ClockMaster::Audio) return false;
    return lagUs > thresholdUs;
}

} // namespace desktopsticker::wallpaper
