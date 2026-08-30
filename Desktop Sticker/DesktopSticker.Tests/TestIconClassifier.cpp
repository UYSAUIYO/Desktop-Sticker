#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/IconClassifier.h>

using namespace desktopsticker;

TEST(Classify_UnknownApp_Default) {
    IconClassifier c;
    ASSERT_STREQ(L"应用", c.ClassifyPath(L"C:\\x\\UnknownTool.exe"));
}

TEST(Classify_DevelopTools) {
    IconClassifier c;
    ASSERT_STREQ(L"开发工具", c.ClassifyPath(L"C:\\x\\Microsoft Visual Studio Code.lnk"));
}

TEST(Classify_Browser) {
    IconClassifier c;
    ASSERT_STREQ(L"浏览器", c.ClassifyPath(L"C:\\x\\chrome.exe"));
}

TEST(Classify_Office) {
    IconClassifier c;
    ASSERT_STREQ(L"办公软件", c.ClassifyPath(L"C:\\x\\WPS Office.lnk"));
}

TEST(Classify_MediaBeatsSocial) {
    // "qq音乐" 同时含 "qq"（社交）与 "qq音乐"（影音）：影音规则在前，必须命中影音
    IconClassifier c;
    ASSERT_STREQ(L"影音娱乐", c.ClassifyPath(L"C:\\x\\QQ音乐.lnk"));
}

TEST(Classify_FlStudioBeatsDev) {
    // "FL Studio" 含 "studio"（开发关键词）：预检规则在前，必须命中影音
    IconClassifier c;
    ASSERT_STREQ(L"影音娱乐", c.ClassifyPath(L"C:\\x\\FL Studio 21.lnk"));
}

TEST(Classify_Social) {
    IconClassifier c;
    ASSERT_STREQ(L"社交聊天", c.ClassifyPath(L"C:\\x\\微信.exe"));
}

TEST(Classify_Game) {
    IconClassifier c;
    ASSERT_STREQ(L"游戏", c.ClassifyPath(L"C:\\x\\Steam.lnk"));
}

TEST(Classify_Utility) {
    IconClassifier c;
    ASSERT_STREQ(L"实用工具", c.ClassifyPath(L"C:\\x\\IDM 6.4.exe"));
}

TEST(Classify_NonAppFile_Others) {
    IconClassifier c;
    ASSERT_STREQ(L"其他", c.ClassifyPath(L"C:\\fake\\movie.mp4"));
    ASSERT_STREQ(L"其他", c.ClassifyPath(L"C:\\fake\\photo.png"));
}

TEST(Classify_Directories) {
    auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Classify";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / L"我的游戏");
    std::filesystem::create_directories(root / L"电影");
    std::filesystem::create_directories(root / L"资料");

    IconClassifier c;
    ASSERT_STREQ(L"游戏", c.ClassifyPath((root / L"我的游戏").wstring()));
    ASSERT_STREQ(L"影音娱乐", c.ClassifyPath((root / L"电影").wstring()));
    ASSERT_STREQ(L"文件夹", c.ClassifyPath((root / L"资料").wstring()));
    std::filesystem::remove_all(root);
}
