#pragma once

#include <cstdint>

namespace desktopsticker::wallpaper {

// 时间单位统一为 QPC 的 100ns，避免浮点与单位混用。
struct FrameScheduleDecision {
    int64_t waitHundredNs = 0;
    bool skipBacklog = false;
};

// 正好踩到期限不算错过：只有已经**超过**期限才跳积压。
inline FrameScheduleDecision schedule_frame(int64_t nowQpc, int64_t deadlineQpc) {
    if (nowQpc <= deadlineQpc) return {deadlineQpc - nowQpc, false};
    return {0, true};
}

inline int64_t next_deadline(int64_t previousDeadline, int64_t frameDuration) {
    return previousDeadline + frameDuration;
}

// 积压后不追赶：返回第一个不早于 now 的期限，丢弃已错过的帧。
inline int64_t next_deadline_not_before(int64_t previousDeadline,
                                        int64_t frameDuration,
                                        int64_t nowQpc) {
    if (frameDuration <= 0) return nowQpc;
    int64_t next = previousDeadline;
    if (next <= nowQpc) {
        const int64_t missed = (nowQpc - next) / frameDuration;
        next += (missed + 1) * frameDuration;
    }
    return next;
}

} // namespace desktopsticker::wallpaper
