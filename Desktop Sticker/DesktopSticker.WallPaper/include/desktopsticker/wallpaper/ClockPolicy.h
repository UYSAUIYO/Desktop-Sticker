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

// 节拍用的时间取值：**必须与主时钟同源**。
//
// 这一层单独提出来是因为踩过坑：曾把 now 无条件取成音频时钟、而 master 仍判为 QPC，
// 两个时钟纪元相差两个数量级（QPC 计数 vs 音频微秒），导致每帧等待被夹到上限、
// 帧率掉到约 10fps。单测覆盖 choose_clock_master 是抓不到"取值与主时钟不一致"的。
struct TimeSource {
    ClockMaster master = ClockMaster::Qpc;
    int64_t now100ns = 0;
};

inline TimeSource pick_time_source(const ClockInputs& in, int64_t qpc100ns, int64_t audioUs) {
    ClockInputs inputs = in;
    inputs.audioClockValid = inputs.audioClockValid && (audioUs >= 0);

    TimeSource out;
    out.master = choose_clock_master(inputs);
    out.now100ns = (out.master == ClockMaster::Audio) ? audioUs * 10 : qpc100ns; // us → 100ns
    return out;
}

} // namespace desktopsticker::wallpaper
