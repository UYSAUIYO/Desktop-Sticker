#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/ConfigStore.h>

using namespace desktopsticker;

TEST(DefaultConfig_IsSavedAndLoaded) {
    auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Config";
    std::filesystem::remove_all(root);

    ConfigStore store(root);
    ASSERT_TRUE(store.Save());

    ConfigStore loaded(root);
    ASSERT_TRUE(loaded.Load());
    ASSERT_STREQ(L"double-space", loaded.GetConfig().hotkeyMode);
    ASSERT_STREQ(L"Alt+Space", loaded.GetConfig().customHotkey);
    ASSERT_TRUE(loaded.GetConfig().searchDesktop);
}

TEST(CorruptedConfig_IsBackedUpAndReset) {
    auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Corrupt";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream out(root / L"config.json");
        out << "{ this is not json";
    }

    ConfigStore store(root);
    ASSERT_TRUE(store.Load());
    ASSERT_STREQ(L"double-space", store.GetConfig().hotkeyMode);
    ASSERT_TRUE(std::filesystem::exists(root / L"config.json.bak"));
}
