#pragma once

#include <cstdint>
#include <map>

#include "desktopsticker/resmon/CpuMath.h"
#include "desktopsticker/resmon/JsonBuild.h"

namespace desktopsticker::resmon {

// QPC → 100ns。频率为 0 时返回 0（调用方按 baseline 处理）。
int64_t qpc_now_100ns();

// 按线程/子进程采样 CPU。采样器只做系统调用与状态持有，
// 百分比计算全部下沉到 CpuMath.h 的纯函数。
class ProcessSampler {
public:
    CpuSnapshot Sample();
    // 供 MemorySampler 复用，避免重复写进程枚举
    std::vector<ChildRow> SampleChildren();

private:
    std::map<uint32_t, CpuSample> prevThreads_;
    std::map<uint32_t, CpuSample> prevChildren_;
};

} // namespace desktopsticker::resmon
