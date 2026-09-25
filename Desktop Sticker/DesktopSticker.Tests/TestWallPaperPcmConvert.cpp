#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/PcmConvert.h>

using namespace desktopsticker::wallpaper;

TEST(PcmMap_SameChannelCountIsPassthrough) {
    const int16_t in[] = { 1, 2, 3, 4 };   // 2 帧 × 2 声道
    std::vector<int16_t> out;
    pcm_map_channels(in, 2, 2, 2, out);
    ASSERT_EQ(static_cast<size_t>(4), out.size());
    ASSERT_EQ(static_cast<int16_t>(1), out[0]);
    ASSERT_EQ(static_cast<int16_t>(4), out[3]);
}

TEST(PcmMap_MonoToStereoDuplicates) {
    const int16_t in[] = { 7, 9 };
    std::vector<int16_t> out;
    pcm_map_channels(in, 2, 1, 2, out);
    ASSERT_EQ(static_cast<size_t>(4), out.size());
    ASSERT_EQ(static_cast<int16_t>(7), out[0]);
    ASSERT_EQ(static_cast<int16_t>(7), out[1]);
    ASSERT_EQ(static_cast<int16_t>(9), out[2]);
    ASSERT_EQ(static_cast<int16_t>(9), out[3]);
}

TEST(PcmMap_StereoToMonoAverages) {
    const int16_t in[] = { 10, 20, 30, 40 };
    std::vector<int16_t> out;
    pcm_map_channels(in, 2, 2, 1, out);
    ASSERT_EQ(static_cast<size_t>(2), out.size());
    ASSERT_EQ(static_cast<int16_t>(15), out[0]);
    ASSERT_EQ(static_cast<int16_t>(35), out[1]);
}

TEST(PcmMap_InvalidInputProducesNothing) {
    std::vector<int16_t> out;
    pcm_map_channels(nullptr, 2, 2, 2, out);
    pcm_map_channels(reinterpret_cast<const int16_t*>("x"), 0, 2, 2, out);
    ASSERT_TRUE(out.empty());
}

TEST(PcmRate_OneToOneIsIdentity) {
    PcmRateConverter c;
    const int16_t in[] = { 0, 1, 2, 3 };
    std::vector<int16_t> out;
    c.Process(in, 4, 1, 1.0, out);
    ASSERT_EQ(static_cast<size_t>(4), out.size());
    for (int i = 0; i < 4; ++i) ASSERT_EQ(static_cast<int16_t>(i), out[i]);
}

TEST(PcmRate_PhaseCarriesAcrossBlocks) {
    PcmRateConverter c;
    std::vector<int16_t> out;
    const int16_t a[] = { 0, 1, 2, 3 };
    const int16_t b[] = { 4, 5 };
    c.Process(a, 4, 1, 1.0, out);
    c.Process(b, 2, 1, 1.0, out);
    ASSERT_EQ(static_cast<size_t>(6), out.size());
    for (int i = 0; i < 6; ++i) ASSERT_EQ(static_cast<int16_t>(i), out[i]);
}

TEST(PcmRate_HalfRatioDoublesOutput) {
    PcmRateConverter c;
    const int16_t in[] = { 0, 1, 2, 3 };
    std::vector<int16_t> out;
    c.Process(in, 4, 1, 0.5, out);   // ratio<1 = 输出更多样本 = 变慢
    ASSERT_EQ(static_cast<size_t>(8), out.size());
    ASSERT_EQ(static_cast<int16_t>(0), out[0]);
    ASSERT_EQ(static_cast<int16_t>(1), out[2]);
    ASSERT_EQ(static_cast<int16_t>(3), out[7]);
}

TEST(PcmRate_DoubleRatioHalvesOutput) {
    PcmRateConverter c;
    const int16_t in[] = { 0, 1, 2, 3 };
    std::vector<int16_t> out;
    c.Process(in, 4, 1, 2.0, out);   // ratio>1 = 输出更少样本 = 变快
    ASSERT_EQ(static_cast<size_t>(2), out.size());
    ASSERT_EQ(static_cast<int16_t>(0), out[0]);
    ASSERT_EQ(static_cast<int16_t>(2), out[1]);
}

TEST(PcmRate_PreservesChannelCount) {
    PcmRateConverter c;
    const int16_t in[] = { 1, 2, 3, 4 };
    std::vector<int16_t> out;
    c.Process(in, 2, 2, 1.0, out);
    ASSERT_EQ(static_cast<size_t>(4), out.size());
    ASSERT_EQ(static_cast<int16_t>(1), out[0]);
    ASSERT_EQ(static_cast<int16_t>(2), out[1]);
}

TEST(PcmRate_InvalidInputsProduceNothing) {
    PcmRateConverter c;
    std::vector<int16_t> out;
    const int16_t in[] = { 1, 2 };
    c.Process(nullptr, 2, 1, 1.0, out);
    c.Process(in, 0, 1, 1.0, out);
    c.Process(in, 2, 0, 1.0, out);
    c.Process(in, 2, 1, 0.0, out);
    c.Process(in, 2, 1, -1.0, out);
    ASSERT_TRUE(out.empty());
}

TEST(PcmRate_ResetClearsPhase) {
    PcmRateConverter c;
    std::vector<int16_t> out;
    const int16_t in[] = { 0, 1, 2, 3 };
    c.Process(in, 4, 1, 0.5, out);
    out.clear();
    c.Reset();
    c.Process(in, 4, 1, 1.0, out);
    ASSERT_EQ(static_cast<size_t>(4), out.size());
    ASSERT_EQ(static_cast<int16_t>(0), out[0]);
}
