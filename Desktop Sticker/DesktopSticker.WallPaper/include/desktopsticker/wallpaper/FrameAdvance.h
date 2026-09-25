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

struct FrameAdvance {
    // 本拍应消费（解码并丢弃）的帧数：0 表示不推进，保持当前帧
    int consume = 0;
    // 本拍是否应向呈现层提交一帧（保持帧时也提交，才能把画面顶住）
    bool present = true;
};

// 以"源帧时长"为基准：speed>1 时每个呈现周期要吞掉多帧，speed<1 时不足一帧就不推进。
// elapsedMs 为本拍实际经过的毫秒数（QPC 差值）；frameDurationMs 为源帧时长。
inline FrameAdvance frame_advance_policy(double speed, int64_t elapsedMs, int64_t frameDurationMs) {
    FrameAdvance out;
    if (frameDurationMs <= 0) return out;      // 源帧时长非法：不推进，避免死循环
    if (elapsedMs <= 0) return out;            // 本拍没有时间流逝：保持

    const double s = clamp_speed(speed);
    // 需要推进的"源帧数"= 经过时间 × 速度 / 源帧时长
    const double frames = static_cast<double>(elapsedMs) * s / static_cast<double>(frameDurationMs);

    if (frames < 1.0) {
        out.consume = 0;                       // 不足一帧：保持当前帧
        out.present = true;
        return out;
    }

    int consume = static_cast<int>(frames);    // 向下取整：不超前消费
    if (consume < 1) consume = 1;
    // 限幅：一次最多吞 8 帧，避免长时间停滞后瞬间冲掉半个视频
    if (consume > 8) consume = 8;
    out.consume = consume;
    out.present = true;
    return out;
}

} // namespace desktopsticker::wallpaper
