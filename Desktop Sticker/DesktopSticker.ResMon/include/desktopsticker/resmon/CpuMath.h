#pragma once

#include <cstdint>

namespace desktopsticker::resmon {

// 时间单位统一为 100ns（GetThreadTimes 的 FILETIME 与 QPC 换算后）
struct CpuSample {
    int64_t kernel100ns = 0;
    int64_t user100ns = 0;
    int64_t wall100ns = 0;
};

struct CpuDelta {
    double percent = 0.0;
    bool baseline = false; // true = 本次无有效基线（首次采样 / 墙钟为 0 / 计数回绕）
};

// 单线程上限 100%（不折算多核总容量）；无法计算时 baseline=true、percent=0
inline CpuDelta cpu_percent(const CpuSample& prev, const CpuSample& now) {
    CpuDelta d;
    const int64_t wall = now.wall100ns - prev.wall100ns;
    if (wall <= 0) {
        d.baseline = true;
        return d;
    }
    const int64_t busy = (now.kernel100ns - prev.kernel100ns) +
                         (now.user100ns - prev.user100ns);
    if (busy < 0) {
        d.baseline = true;
        return d;
    }
    d.percent = static_cast<double>(busy) * 100.0 / static_cast<double>(wall);
    if (d.percent > 100.0) d.percent = 100.0;
    return d;
}

} // namespace desktopsticker::resmon
