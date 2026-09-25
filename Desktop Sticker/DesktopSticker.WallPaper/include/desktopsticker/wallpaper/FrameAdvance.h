#pragma once

// 调速下的帧推进策略（纯函数）。
// 播放速度由我们自己的调度实现：>1× 需要丢帧追赶，<1× 需要保持当前帧。

#include <cstdint>

namespace desktopsticker::wallpaper {

// 合法速度区间（规格 §11：0.25×–4×）
inline constexpr double kMinSpeed = 0.25;
inline constexpr double kMaxSpeed = 4.0;

inline double clamp_speed(double speed) {
    if (!(speed > 0.0)) return 1.0;          // NaN / 0 / 负数一律回落到 1×
    if (speed < kMinSpeed) return kMinSpeed;
    if (speed > kMaxSpeed) return kMaxSpeed;
    return speed;
}

// 跨调用累积的"不足一帧"的余量。
//
// 这个累加器是必需的：调度节拍天然抖动（实测 30ms/46ms/62ms 交替），
// 若每次只做 `floor(elapsed × speed / frameDuration)` 而不留下小数余量，
// 那么"不足一帧"的拍会保持、而"超过一帧"的拍只会消费 1 帧，
// 余量被逐次丢弃 → 视频持续落后。实测 30fps 素材只出 9 帧/秒。
struct FrameAdvanceState {
    double debt = 0.0;   // 已积累但尚未消费的"源帧数"
    void Reset() { debt = 0.0; }
};

struct FrameAdvance {
    // 本拍应消费（解码并丢弃）的帧数：0 表示不推进，保持当前帧
    int consume = 0;
    // 本拍是否应向呈现层提交一帧（保持帧时也提交，才能把画面顶住）
    bool present = true;
};

// 单拍上限：长时间停滞后不能一次吞掉半个视频
inline constexpr int kMaxConsumePerTick = 8;

// 以"源帧时长"为基准推进。余量跨调用保留，所以长期平均消费帧数
// 恰好等于 speed × 经过时间 / 源帧时长。
inline FrameAdvance frame_advance_policy(FrameAdvanceState& state, double speed, int64_t elapsedMs,
                                        int64_t frameDurationMs) {
    FrameAdvance out;
    if (frameDurationMs <= 0) return out;      // 源帧时长非法：不推进，避免死循环
    if (elapsedMs <= 0) return out;            // 本拍没有时间流逝：保持

    const double s = clamp_speed(speed);
    state.debt += static_cast<double>(elapsedMs) * s / static_cast<double>(frameDurationMs);

    if (state.debt < 1.0) {
        out.consume = 0;                       // 不足一帧：保持当前帧，余量留到下一拍
        out.present = true;
        return out;
    }

    int consume = static_cast<int>(state.debt);   // 向下取整
    if (consume < 1) consume = 1;
    if (consume > kMaxConsumePerTick) {
        consume = kMaxConsumePerTick;             // 限幅：超出部分丢弃，避免停滞后暴冲
        state.debt = 0.0;
    } else {
        state.debt -= consume;                    // 关键：保留小数余量
    }
    out.consume = consume;
    out.present = true;
    return out;
}

} // namespace desktopsticker::wallpaper
