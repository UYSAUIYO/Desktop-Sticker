#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/FfmpegCommand.h>

#include <algorithm>

using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

static bool contains(const std::vector<std::wstring>& v, const std::wstring& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// 取某个开关后面紧跟的参数值；未找到返回空串。
static std::wstring arg_after(const std::vector<std::wstring>& v, const std::wstring& flag) {
    auto it = std::find(v.begin(), v.end(), flag);
    if (it == v.end() || it + 1 == v.end()) return L"";
    return *(it + 1);
}

static int crf_of(const std::vector<std::wstring>& v) {
    const std::wstring s = arg_after(v, L"-crf");
    return s.empty() ? -1 : static_cast<int>(std::wcstol(s.c_str(), nullptr, 10));
}

TEST(FfmpegCommand_InputIsSource_OutputIsVariant) {
    auto a = build_transcode_args(L"C:\\x\\source.mp4", L"D:\\lib\\balanced-v1.mp4",
                                  VariantKind::Balanced);
    ASSERT_TRUE(contains(a, L"-i"));
    ASSERT_STREQ(L"C:\\x\\source.mp4", arg_after(a, L"-i"));      // 输入是源文件
    ASSERT_STREQ(L"D:\\lib\\balanced-v1.mp4", a.back());          // 输出是派生副本
}

TEST(FfmpegCommand_NeverWritesBackToSource) {
    auto a = build_transcode_args(L"C:\\x\\source.mp4", L"D:\\lib\\balanced-v1.mp4",
                                  VariantKind::Balanced);
    // 源路径只能作为 -i 的取值出现，绝不能成为输出
    ASSERT_TRUE(a.back() != L"C:\\x\\source.mp4");
}

TEST(FfmpegCommand_NoAudioTrack) {
    auto a = build_transcode_args(L"a.mp4", L"b.mp4", VariantKind::Balanced);
    ASSERT_TRUE(contains(a, L"-an"));            // 壁纸静音，不输出音轨
}

TEST(FfmpegCommand_PowerSaverIsSmallerThanBalanced) {
    auto bal = build_transcode_args(L"a.mp4", L"b.mp4", VariantKind::Balanced);
    auto pwr = build_transcode_args(L"a.mp4", L"c.mp4", VariantKind::PowerSaver);
    ASSERT_TRUE(crf_of(pwr) > crf_of(bal));      // 省电档 CRF 更大 = 码率更低
}

TEST(FfmpegCommand_OriginalHasNoTranscodeArgs) {
    ASSERT_TRUE(build_transcode_args(L"a.mp4", L"b.mp4", VariantKind::Original).empty());
}

TEST(FfmpegCommand_VariantFileName) {
    ASSERT_STREQ(L"balanced-v1.mp4", variant_file_name(VariantKind::Balanced, 1));
    ASSERT_STREQ(L"power-saver-v2.mp4", variant_file_name(VariantKind::PowerSaver, 2));
}
