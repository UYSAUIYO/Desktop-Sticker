#pragma once

#include "ProcessSampler.h"
#include "desktopsticker/resmon/JsonBuild.h"

namespace desktopsticker::resmon {

// 进程内存与已加载模块。不报"虚拟大小"：PSAPI 不提供该字段，
// 为一个展示项引入未文档化的 NtQueryInformationProcess 不值得。
// 模块列表只保留"我们自己的"（自写代码 + 随包 ffmpeg），排除系统公共 DLL 与第三方框架。
class MemorySampler {
public:
    explicit MemorySampler(ProcessSampler& children) : children_(children) {}
    MemorySnapshot Sample(const std::wstring& exeDir);

private:
    ProcessSampler& children_;
};

} // namespace desktopsticker::resmon
