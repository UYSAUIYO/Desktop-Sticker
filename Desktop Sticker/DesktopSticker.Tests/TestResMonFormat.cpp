#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/Format.h>

#include <numeric>

using namespace desktopsticker::resmon;

TEST(Format_BytesIntegerBelowKb) {
    ASSERT_STREQ(L"0 B", format_bytes(0));
    ASSERT_STREQ(L"1023 B", format_bytes(1023));
}

TEST(Format_BytesStripsTrailingZero) {
    ASSERT_STREQ(L"1 KB", format_bytes(1024));
    ASSERT_STREQ(L"4 KB", format_bytes(4096));
}

TEST(Format_BytesKeepsOneDecimal) {
    ASSERT_STREQ(L"54.3 GB", format_bytes(58300000000ull));
    ASSERT_STREQ(L"1.5 MB", format_bytes(1572864ull));
}

TEST(Format_BytesTeraScale) {
    ASSERT_STREQ(L"1 TB", format_bytes(1024ull * 1024 * 1024 * 1024));
}

TEST(Format_PercentOneDecimal) {
    ASSERT_STREQ(L"54.3%", format_percent(54.28));
    ASSERT_STREQ(L"0%", format_percent(0.0));
    ASSERT_STREQ(L"100%", format_percent(100.0));
}

TEST(Percentages_ZeroTotal_AllZero) {
    const auto p = compute_percentages({0, 0, 0});
    ASSERT_EQ(static_cast<size_t>(3), p.size());
    ASSERT_TRUE(p[0] == 0.0 && p[1] == 0.0 && p[2] == 0.0);
}

TEST(Percentages_SingleCategoryIsHundred) {
    const auto p = compute_percentages({12345});
    ASSERT_EQ(1u, static_cast<unsigned>(p.size()));
    ASSERT_TRUE(p[0] == 100.0);
}

TEST(Percentages_SumIsExactlyHundred) {
    // 三个接近三等分的份额，四舍五入后必然有偏差，必须由最大类补差修平
    const auto p = compute_percentages({333333, 333333, 333334});
    const double sum = std::accumulate(p.begin(), p.end(), 0.0);
    ASSERT_TRUE(sum > 99.999 && sum < 100.001);
}

TEST(Percentages_EqualSharesTieBreakIsDeterministic) {
    // 三等分必然产生舍入余量，补差给下标最小的最大类，结果必须确定
    const auto p = compute_percentages({1, 1, 1});
    const double sum = std::accumulate(p.begin(), p.end(), 0.0);
    ASSERT_TRUE(sum > 99.999 && sum < 100.001);
    ASSERT_TRUE(p[0] >= p[1] && p[0] >= p[2]);
    ASSERT_TRUE(p[1] == p[2]);
}

TEST(Percentages_RealWorldStorageMix) {
    // 实测基线：壁纸 352MB / ffmpeg 158MB / pdb 72MB / assets 3.2MB / 其余很小
    const std::vector<uint64_t> bytes = {
        352ull * 1024 * 1024, 158ull * 1024 * 1024, 72ull * 1024 * 1024,
        3ull * 1024 * 1024, 300ull * 1024,
    };
    const auto p = compute_percentages(bytes);
    const double sum = std::accumulate(p.begin(), p.end(), 0.0);
    ASSERT_TRUE(sum > 99.999 && sum < 100.001);
    ASSERT_TRUE(p[0] > p[1] && p[1] > p[2]); // 顺序保持，且壁纸最大
}
