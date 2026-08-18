#include "pch.h"
#include "desktopsticker/ConfigStore.h"

#include <nlohmann/json.hpp>

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
            cfg.hotkeyMode = std::wstring(j["hotkeyMode"].get<std::string>().begin(), j["hotkeyMode"].get<std::string>().end());
        }
        if (j.contains("customHotkey") && j["customHotkey"].is_string()) {
            cfg.customHotkey = std::wstring(j["customHotkey"].get<std::string>().begin(), j["customHotkey"].get<std::string>().end());
        }
        cfg.followSystemTheme = j.value("followSystemTheme", true);
        cfg.searchDesktop = j.value("searchDesktop", true);
        cfg.searchKnownFolders = j.value("searchKnownFolders", true);
        cfg.includeHiddenFiles = j.value("includeHiddenFiles", false);
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
    std::string hotkeyMode(config_.hotkeyMode.begin(), config_.hotkeyMode.end());
    std::string customHotkey(config_.customHotkey.begin(), config_.customHotkey.end());
    j["hotkeyMode"] = hotkeyMode;
    j["customHotkey"] = customHotkey;
    j["followSystemTheme"] = config_.followSystemTheme;
    j["searchDesktop"] = config_.searchDesktop;
    j["searchKnownFolders"] = config_.searchKnownFolders;
    j["includeHiddenFiles"] = config_.includeHiddenFiles;

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
