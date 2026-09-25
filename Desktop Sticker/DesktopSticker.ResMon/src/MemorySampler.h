#pragma once

#include "ProcessSampler.h"
#include "desktopsticker/resmon/JsonBuild.h"

namespace desktopsticker::resmon {

// 进程内存与已加载模块。不报"虚拟大小"：PSAPI 不提供该字段，
// 为一个展示项引入未文档化的 NtQueryInformationProcess 不值得。
class MemorySampler {
public:
    explicit MemorySampler(ProcessSampler& children) : children_(children) {}
    MemorySnapshot Sample();

private:
    ProcessSampler& children_;
};

} // namespace desktopsticker::resmon
