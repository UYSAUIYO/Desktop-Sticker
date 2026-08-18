#pragma once
#include <filesystem>
#include <string>

#include "desktopsticker/Export.h"

namespace desktopsticker {

struct AppConfig {
    std::wstring hotkeyMode = L"double-space"; // "double-space" | "custom"
    std::wstring customHotkey = L"Alt+Space";
    bool followSystemTheme = true;
    bool searchDesktop = true;
    bool searchKnownFolders = true;
    bool includeHiddenFiles = false;
};

class DESKTOPSTICKER_API ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path rootDir);

    bool Load();
    bool Save() const;

    const AppConfig& GetConfig() const { return config_; }
    void SetConfig(const AppConfig& config) { config_ = config; }
    std::filesystem::path GetRootDir() const { return rootDir_; }

private:
    std::filesystem::path rootDir_;
    std::filesystem::path configPath_;
    AppConfig config_;
};

} // namespace desktopsticker
