#include "pch.h"
#include "desktopsticker/ConfigStore.h"

#include <nlohmann/json.hpp>

#include "desktopsticker/Utf8.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

ConfigStore::ConfigStore(std::filesystem::path rootDir)
    : rootDir_(std::move(rootDir)), configPath_(rootDir_ / L"config.json") {}

bool ConfigStore::Load() {
    std::error_code ec;
    fs::create_directories(rootDir_, ec);
    if (!fs::exists(configPath_)) {
        return Save();
    }

    try {
        std::ifstream in(configPath_);
        if (!in.is_open()) return false;
        json j;
        in >> j;

        AppConfig cfg;
        if (j.contains("hotkeyMode") && j["hotkeyMode"].is_string()) {
            cfg.hotkeyMode = FromUtf8(j["hotkeyMode"].get<std::string>());
        }
        if (j.contains("customHotkey") && j["customHotkey"].is_string()) {
            cfg.customHotkey = FromUtf8(j["customHotkey"].get<std::string>());
        }
        cfg.followSystemTheme = j.value("followSystemTheme", true);
        cfg.searchDesktop = j.value("searchDesktop", true);
        cfg.searchKnownFolders = j.value("searchKnownFolders", true);
        cfg.searchStartMenu = j.value("searchStartMenu", true);
        cfg.includeHiddenFiles = j.value("includeHiddenFiles", false);
        cfg.zoneColumnSpacing = j.value("zoneColumnSpacing", 48);
        cfg.zoneRowSpacing = j.value("zoneRowSpacing", 72);
        config_ = cfg;
        return true;
    } catch (...) {
        // 损坏时备份后重置
        std::error_code backupEc;
        fs::copy_file(configPath_, configPath_.wstring() + L".bak", fs::copy_options::overwrite_existing, backupEc);
        config_ = AppConfig{};
        return Save();
    }
}

bool ConfigStore::Save() const {
    std::error_code ec;
    fs::create_directories(rootDir_, ec);

    json j;
    j["hotkeyMode"] = ToUtf8(config_.hotkeyMode);
    j["customHotkey"] = ToUtf8(config_.customHotkey);
    j["followSystemTheme"] = config_.followSystemTheme;
    j["searchDesktop"] = config_.searchDesktop;
    j["searchKnownFolders"] = config_.searchKnownFolders;
    j["searchStartMenu"] = config_.searchStartMenu;
    j["includeHiddenFiles"] = config_.includeHiddenFiles;
    j["zoneColumnSpacing"] = config_.zoneColumnSpacing;
    j["zoneRowSpacing"] = config_.zoneRowSpacing;

    const fs::path tmp = configPath_.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << j.dump(2);
        out.flush();
    }
    // 原子替换
    return MoveFileExW(tmp.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

} // namespace desktopsticker
