#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/DecodeTarget.h>

using namespace desktopsticker::wallpaper;

TEST(DecodeTarget_4kSourceOn1080pScreenHalvesEachAxis) {
    const ImageSize t = decode_target_size(3840, 2160, 1920, 1080);
    ASSERT_EQ(1920, t.width);
    ASSERT_EQ(1080, t.height);
}

TEST(DecodeTarget_SmallerSourceIsNeverUpscaled) {
    const ImageSize t = decode_target_size(1280, 720, 1920, 1080);
    ASSERT_EQ(1280, t.width);
    ASSERT_EQ(720, t.height);
}

TEST(DecodeTarget_PreservesAspectRatioOnWideSource) {
    // 21:9 源缩到 16:9 窗口：受高度限制，宽度按比例留边
    const ImageSize t = decode_target_size(2560, 1080, 1920, 1080);
    ASSERT_EQ(1920, t.width);
    ASSERT_EQ(810, t.height);
}

TEST(DecodeTarget_PreservesAspectRatioOnTallSource) {
    // 4:3 源缩到 16:9 窗口：受宽度限制
    const ImageSize t = decode_target_size(3840, 2880, 1920, 1080);
    ASSERT_EQ(1440, t.width);
    ASSERT_EQ(1080, t.height);
}

TEST(DecodeTarget_AxisIsRoundedToEven) {
    // 1000×1000 → 受高度限制 1080 不需缩，受宽限制 1920 → 不缩；改用大源逼出奇数
    const ImageSize t = decode_target_size(1000, 999, 501, 1080);
    ASSERT_EQ(0, t.width % 2);
    ASSERT_EQ(0, t.height % 2);
    ASSERT_TRUE(t.width <= 501);
    ASSERT_TRUE(t.height <= 1080);
}

TEST(DecodeTarget_NoLimitMeansSourceSize) {
    const ImageSize t = decode_target_size(3840, 2160, 0, 0);
    ASSERT_EQ(3840, t.width);
    ASSERT_EQ(2160, t.height);
}

TEST(DecodeTarget_UnknownSourceSizeStaysUnspecified) {
    // 源尺寸问不到时不能瞎算，交给调用方退化到源尺寸
    const ImageSize t = decode_target_size(0, 0, 1920, 1080);
    ASSERT_EQ(0, t.width);
    ASSERT_EQ(0, t.height);
}

TEST(DecodeTarget_NegativeSourceSizeStaysUnspecified) {
    const ImageSize t = decode_target_size(-1, -1, 1920, 1080);
    ASSERT_EQ(0, t.width);
    ASSERT_EQ(0, t.height);
}

TEST(DecodeTarget_OneSidedLimitIsTreatedAsNoLimit) {
    // 只给一个轴上界时无法做等比缩放，一律按"不限制"处理而不是拿 0 去做除数
    const ImageSize t = decode_target_size(3840, 2160, 1920, 0);
    ASSERT_EQ(3840, t.width);
    ASSERT_EQ(2160, t.height);
}

TEST(DecodeTarget_TinyLimitKeepsMinimumSize) {
    // 上限极端小的时候保底 2 像素，不能算出 0 尺寸导致后续除零/非法纹理
    const ImageSize t = decode_target_size(3840, 2160, 3, 2);
    ASSERT_TRUE(t.width >= 2);
    ASSERT_TRUE(t.height >= 2);
    ASSERT_TRUE(t.width <= 3);
    ASSERT_TRUE(t.height <= 2);
}

TEST(DecodeTarget_ExactFitIsUnchanged) {
    const ImageSize t = decode_target_size(1920, 1080, 1920, 1080);
    ASSERT_EQ(1920, t.width);
    ASSERT_EQ(1080, t.height);
}
