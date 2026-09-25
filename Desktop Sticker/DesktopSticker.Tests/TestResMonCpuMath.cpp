#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/CpuMath.h>

using namespace desktopsticker::resmon;

TEST(CpuMath_HalfBusy) {
    CpuSample prev{0, 0, 0};
    CpuSample now{500'000'000ll, 500'000'000ll, 2'000'000'000ll}; // 忙 1s / 墙钟 2s
    const auto d = cpu_percent(prev, now);
    ASSERT_FALSE(d.baseline);
    ASSERT_TRUE(d.percent > 49.9 && d.percent < 50.1);
}

TEST(CpuMath_FirstSampleHasNoBaseline) {
    CpuSample prev{};
    CpuSample now{100'000'000ll, 0, 0}; // 墙钟差为 0
    const auto d = cpu_percent(prev, now);
    ASSERT_TRUE(d.baseline);
    ASSERT_TRUE(d.percent == 0.0);
}

TEST(CpuMath_ZeroWallClockIsBaseline) {
    CpuSample prev{0, 0, 1000};
    CpuSample now{100, 0, 1000};
    ASSERT_TRUE(cpu_percent(prev, now).baseline);
}

TEST(CpuMath_NegativeBusyIsBaseline) {
    // 线程被复用或计数回绕时不得给出负数
    CpuSample prev{1000, 1000, 0};
    CpuSample now{10, 10, 1'000'000'000ll};
    const auto d = cpu_percent(prev, now);
    ASSERT_TRUE(d.baseline);
    ASSERT_TRUE(d.percent == 0.0);
}

TEST(CpuMath_CapsAtHundred) {
    CpuSample prev{0, 0, 0};
    CpuSample now{5'000'000'000ll, 0, 1'000'000'000ll}; // 忙 5s / 墙钟 1s
    const auto d = cpu_percent(prev, now);
    ASSERT_TRUE(d.percent <= 100.0);
}
