#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/IndexService.h>

#include <fstream>

using namespace desktopsticker;

namespace {
void WriteFile(const std::filesystem::path& p, const std::wstring& content) {
    std::ofstream out(p, std::ios::binary);
    out << std::string(content.begin(), content.end());
}
}

TEST(Search_FindsFileByName) {
    auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Index";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    WriteFile(root / L"report.docx", L"");

    ConfigStore config(root);
    AppConfig cfg;
    cfg.searchDesktop = false;
    cfg.searchKnownFolders = false;
    config.SetConfig(cfg);
    IndexService index(&config);
    index.AddApp(root / L"report.docx");
    index.Rebuild();

    auto results = index.Search(L"report", 10);
    ASSERT_EQ(1u, results.size());
    ASSERT_STREQ(L"report.docx", results[0].name);
}

TEST(Search_FindsByPinyin) {
    auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Pinyin";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    ConfigStore config(root);
    AppConfig cfg;
    cfg.searchDesktop = false;
    cfg.searchKnownFolders = false;
    config.SetConfig(cfg);
    IndexService index(&config);
    index.AddApp(L"C:\\fake\\微信.exe");
    index.Rebuild();

    auto results = index.Search(L"wx", 10);
    ASSERT_EQ(1u, results.size());
    ASSERT_STREQ(L"C:\\fake\\微信.exe", results[0].path);
}
