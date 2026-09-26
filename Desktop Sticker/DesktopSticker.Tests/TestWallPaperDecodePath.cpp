#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/DecodePath.h>

using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

// 这个字符串会写进 wallpaper.json，映射必须稳定：改了等于改数据格式

TEST(DecodePath_RoundTripsAllValues) {
    const DecodePath all[] = { DecodePath::Auto, DecodePath::FfmpegHardware,
                               DecodePath::MediaFoundationD3d, DecodePath::Cpu };
    for (DecodePath p : all) {
        const std::string s = decode_path_to_string(p);
        ASSERT_TRUE(!s.empty());
        ASSERT_TRUE(decode_path_from_string(s) == p);
    }
}

TEST(DecodePath_StableStrings) {
    // 写死的期望值：以后有人改字符串会在这里炸，提醒他这会影响已落盘的配置
    ASSERT_TRUE(std::string(decode_path_to_string(DecodePath::Auto)) == "auto");
    ASSERT_TRUE(std::string(decode_path_to_string(DecodePath::FfmpegHardware)) == "ffmpeg-hw");
    ASSERT_TRUE(std::string(decode_path_to_string(DecodePath::MediaFoundationD3d)) == "mf-d3d11");
    ASSERT_TRUE(std::string(decode_path_to_string(DecodePath::Cpu)) == "cpu");
}

TEST(DecodePath_LegacyVulkanIdStillReadsAsHardware) {
    // 早期版本把这一档写成 "ffmpeg-vulkan"：已落盘的配置必须继续能用
    ASSERT_TRUE(decode_path_from_string("ffmpeg-vulkan") == DecodePath::FfmpegHardware);
}

TEST(DecodePath_UnknownOrMissingFallsBackToAuto) {
    ASSERT_TRUE(decode_path_from_string("") == DecodePath::Auto);
    ASSERT_TRUE(decode_path_from_string("nonsense") == DecodePath::Auto);
    ASSERT_TRUE(decode_path_from_string("AUTO") == DecodePath::Auto);   // 大小写敏感：不猜
    ASSERT_TRUE(decode_path_from_string("cuvid") == DecodePath::Auto);
}

TEST(DecodePath_HasDisplayNames) {
    const DecodePath all[] = { DecodePath::Auto, DecodePath::FfmpegHardware,
                               DecodePath::MediaFoundationD3d, DecodePath::Cpu };
    for (DecodePath p : all) {
        const wchar_t* n = decode_path_name(p);
        ASSERT_TRUE(n != nullptr && n[0] != L'\0');
    }
}
