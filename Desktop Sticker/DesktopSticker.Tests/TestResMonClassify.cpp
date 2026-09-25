#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/Classify.h>

using namespace desktopsticker::resmon;

namespace {
ClassifyRoots roots() {
    ClassifyRoots r;
    r.exeDir = L"C:\\App";
    r.configDir = L"C:\\Users\\u\\AppData\\Roaming\\DesktopSticker";
    r.wallpaperRoot = L"E:\\DesktopSticker\\Wallpaper";
    return r;
}
} // namespace

TEST(Classify_WallpaperRootAnywhereOnAnyDrive) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\media\\a\\source.mp4", r) ==
                StorageCategory::Wallpaper);
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\library.json", r) ==
                StorageCategory::Wallpaper);
}

TEST(Classify_FfmpegPayload) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\ffmpeg\\ffmpeg.exe", r) == StorageCategory::Ffmpeg);
}

TEST(Classify_FfmpegRuleIsComponentAware) {
    // 前缀相似但不同的目录绝不能命中 ffmpeg 规则
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\ffmpeg-sdk\\include\\x.h", r) == StorageCategory::Runtime);
}

TEST(Classify_AssetsAndPdbAndProgram) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\assets\\weather\\S2\\100.png", r) == StorageCategory::Assets);
    ASSERT_TRUE(classify_path(L"C:\\App\\Desktop_Sticker.pdb", r) == StorageCategory::Pdb);
    ASSERT_TRUE(classify_path(L"C:\\App\\Desktop_Sticker.exe", r) == StorageCategory::Program);
    ASSERT_TRUE(classify_path(L"C:\\App\\DesktopSticker.Features.dll", r) == StorageCategory::Program);
    ASSERT_TRUE(classify_path(L"C:\\App\\DesktopSticker.ResMon.dll", r) == StorageCategory::Program);
}

TEST(Classify_PdbMustBeDirectChildOfExeDir) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\sub\\other.pdb", r) == StorageCategory::Runtime);
}

TEST(Classify_RuntimeIsExeDirCatchAll) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\Microsoft.ui.xaml.dll", r) == StorageCategory::Runtime);
    ASSERT_TRUE(classify_path(L"C:\\App\\resmon\\index.html", r) == StorageCategory::Runtime);
}

TEST(Classify_ConfigAndLogs) {
    const auto r = roots();
    const std::wstring cfg = r.configDir;
    ASSERT_TRUE(classify_path(cfg + L"\\config.json", r) == StorageCategory::Config);
    ASSERT_TRUE(classify_path(cfg + L"\\wallpaper.json", r) == StorageCategory::Config);
    ASSERT_TRUE(classify_path(cfg + L"\\debug.log", r) == StorageCategory::Logs);
    ASSERT_TRUE(classify_path(cfg + L"\\debug.log.bak", r) == StorageCategory::Logs);
    ASSERT_TRUE(classify_path(cfg + L"\\something.txt", r) == StorageCategory::Other);
}

TEST(Classify_IsCaseInsensitive) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"c:\\app\\FFMPEG\\ffmpeg.exe", r) == StorageCategory::Ffmpeg);
}

TEST(Classify_EmptyWallpaperRootSkipsRule) {
    ClassifyRoots r = roots();
    r.wallpaperRoot.clear();
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\library.json", r) ==
                StorageCategory::Other);
}

TEST(Classify_OutsideAllRootsIsOther) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"D:\\somewhere\\else\\x.bin", r) == StorageCategory::Other);
}

TEST(Classify_EveryCategoryHasNameAndId) {
    for (int i = 0; i < static_cast<int>(StorageCategory::Count); ++i) {
        const auto c = static_cast<StorageCategory>(i);
        ASSERT_TRUE(category_name(c) != nullptr && *category_name(c) != L'\0');
        ASSERT_TRUE(category_id(c) != nullptr && *category_id(c) != L'\0');
    }
}

TEST(Classify_RootWithTrailingSeparatorIsTolerated) {
    // 配置来源可能自带尾分隔符，必须与不带尾分隔符等价
    ClassifyRoots r = roots();
    r.exeDir = L"C:\\App\\";
    r.wallpaperRoot = L"E:\\DesktopSticker\\Wallpaper\\";
    ASSERT_TRUE(classify_path(L"C:\\App\\ffmpeg\\ffmpeg.exe", r) == StorageCategory::Ffmpeg);
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\a.mp4", r) ==
                StorageCategory::Wallpaper);
}

// ---- 内存页的模块过滤：只认自写代码与随包 ffmpeg ----

TEST(OwnModule_OurExeAndDlls) {
    const std::wstring exe = L"C:\\App";
    ASSERT_TRUE(is_own_module(L"C:\\App\\Desktop_Sticker.exe", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\DesktopSticker.Features.dll", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\DesktopSticker.WallPaper.dll", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\DesktopSticker.ResMon.dll", exe));
}

TEST(OwnModule_CaseInsensitive) {
    ASSERT_TRUE(is_own_module(L"c:\\app\\desktop_sticker.exe", L"C:\\App"));
}

TEST(OwnModule_BundledFfmpegLibraries) {
    const std::wstring exe = L"C:\\App";
    ASSERT_TRUE(is_own_module(L"C:\\App\\ffmpeg\\avcodec-62.dll", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\ffmpeg\\avformat-62.dll", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\ffmpeg\\avutil-60.dll", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\ffmpeg\\swscale-9.dll", exe));
    ASSERT_TRUE(is_own_module(L"C:\\App\\ffmpeg\\swresample-6.dll", exe));
}

TEST(OwnModule_SystemAvLibraryIsNotOurs) {
    // 同前缀但不在我们的 ffmpeg 负载目录里，绝不能算成随包负载
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\avcodec-99.dll", L"C:\\App"));
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\swscale-9.dll", L"C:\\App"));
}

TEST(OwnModule_SystemPublicDllsExcluded) {
    const std::wstring exe = L"C:\\App";
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\SHELL32.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\windows.storage.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\ieframe.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\Windows.Media.dll", exe));
}

TEST(OwnModule_GpuDriverDllsExcluded) {
    const std::wstring exe = L"C:\\App";
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\DriverStore\\FileRepository\\nv\\nvgpucomp64.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\Windows\\System32\\nvppex.dll", exe));
}

TEST(OwnModule_ThirdPartyFrameworksExcluded) {
    // 自包含部署会把框架 DLL 放在程序目录里，但它们不是我们的资源
    const std::wstring exe = L"C:\\App";
    ASSERT_FALSE(is_own_module(L"C:\\App\\Microsoft.UI.Xaml.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\App\\onnxruntime.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\App\\DirectML.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\App\\WebView2Loader.dll", exe));
    ASSERT_FALSE(is_own_module(L"C:\\App\\Microsoft.WindowsAppRuntime.dll", exe));
}
