#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/BackendKind.h>

#include <algorithm>

using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

TEST(BackendKind_VideoExtensions) {
    BackendKind k{};
    ASSERT_TRUE(classify_file(L"C:\\a\\b.mp4", k) && k == BackendKind::Video);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.MOV", k) && k == BackendKind::Video);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.mkv", k) && k == BackendKind::Video);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.WEBM", k) && k == BackendKind::Video);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.wmv", k) && k == BackendKind::Video);
}

TEST(BackendKind_AnimatedImageExtensions) {
    BackendKind k{};
    ASSERT_TRUE(classify_file(L"C:\\a\\b.gif", k) && k == BackendKind::AnimatedImage);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.WebP", k) && k == BackendKind::AnimatedImage);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.apng", k) && k == BackendKind::AnimatedImage);
}

TEST(BackendKind_StaticImageCountsAsSequence) {
    BackendKind k{};
    ASSERT_TRUE(classify_file(L"C:\\a\\b.png", k) && k == BackendKind::ImageSequence);
    ASSERT_TRUE(classify_file(L"C:\\a\\b.JPEG", k) && k == BackendKind::ImageSequence);
}

TEST(BackendKind_WebAndShaderFiles) {
    BackendKind k{};
    ASSERT_TRUE(classify_file(L"C:\\a\\index.html", k) && k == BackendKind::Web);
    ASSERT_TRUE(classify_file(L"C:\\a\\main.frag", k) && k == BackendKind::Shader3D);
    ASSERT_TRUE(classify_file(L"C:\\a\\scene.gltf", k) && k == BackendKind::Shader3D);
    ASSERT_TRUE(classify_file(L"C:\\a\\model.OBJ", k) && k == BackendKind::Shader3D);
}

TEST(BackendKind_UnknownExtensionRejected) {
    BackendKind k{};
    ASSERT_FALSE(classify_file(L"C:\\a\\b.txt", k));
    ASSERT_FALSE(classify_file(L"C:\\a\\noext", k));
}

TEST(BackendKind_DirectoryPriorityWebWins) {
    // 同时含 index.html 与着色器时，Web 优先（最具体）
    const std::vector<std::wstring> entries = { L"index.html", L"main.frag", L"pic.png" };
    BackendKind k{};
    ASSERT_TRUE(classify_directory(entries, k) && k == BackendKind::Web);
}

TEST(BackendKind_DirectoryShaderBeatsImages) {
    const std::vector<std::wstring> entries = { L"main.frag", L"pic.png" };
    BackendKind k{};
    ASSERT_TRUE(classify_directory(entries, k) && k == BackendKind::Shader3D);
}

TEST(BackendKind_DirectoryImagesOnlyIsSequence) {
    const std::vector<std::wstring> entries = { L"1.png", L"2.jpg", L"notes.txt" };
    BackendKind k{};
    ASSERT_TRUE(classify_directory(entries, k) && k == BackendKind::ImageSequence);
}

TEST(BackendKind_DirectoryUnrecognised) {
    BackendKind k{};
    ASSERT_FALSE(classify_directory({}, k));
    ASSERT_FALSE(classify_directory({ L"readme.txt", L"data.bin" }, k));
}

TEST(BackendKind_NaturalSortOrdersNumerically) {
    const auto out = natural_sort_image_frames(
        { L"img10.png", L"img2.png", L"img1.png", L"img20.png" });
    ASSERT_EQ(static_cast<size_t>(4), out.size());
    ASSERT_STREQ(L"img1.png", out[0]);
    ASSERT_STREQ(L"img2.png", out[1]);
    ASSERT_STREQ(L"img10.png", out[2]);
    ASSERT_STREQ(L"img20.png", out[3]);
}

TEST(BackendKind_NaturalSortIgnoresLeadingZeros) {
    const auto out = natural_sort_image_frames({ L"f002.png", L"f1.png", L"f010.png" });
    ASSERT_EQ(static_cast<size_t>(3), out.size());
    ASSERT_STREQ(L"f1.png", out[0]);
    ASSERT_STREQ(L"f002.png", out[1]);
    ASSERT_STREQ(L"f010.png", out[2]);
}

TEST(BackendKind_NaturalSortDropsNonImages) {
    const auto out = natural_sort_image_frames({ L"a.png", L"b.txt", L"c.mp4", L"d.jpg" });
    ASSERT_EQ(static_cast<size_t>(2), out.size());
    ASSERT_STREQ(L"a.png", out[0]);
    ASSERT_STREQ(L"d.jpg", out[1]);
}

TEST(BackendKind_NaturalSortIsCaseInsensitiveAndStrict) {
    const auto out = natural_sort_image_frames({ L"B.png", L"a.png" });
    ASSERT_EQ(static_cast<size_t>(2), out.size());
    ASSERT_STREQ(L"a.png", out[0]);
    ASSERT_STREQ(L"B.png", out[1]);
}
