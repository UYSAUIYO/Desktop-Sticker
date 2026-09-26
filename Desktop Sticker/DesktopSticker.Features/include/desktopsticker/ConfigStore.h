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
    bool searchStartMenu = true; // 搜索开始菜单快捷方式（应用的主要来源）
    bool includeHiddenFiles = false;
    // 磁贴网格间距（像素）
    int zoneColumnSpacing = 48;
    int zoneRowSpacing = 72;
    // 每列卡片数（4 或 5）：决定初始四列布局的列容量，卡片高度动态铺满到任务栏
    int zoneColumnCards = 4;
    // 桌面时钟小组件（顶部居中，点击穿透）
    bool showClock = true;
    // 双击桌面空白处（左键）切换磁贴/时钟/图标的显隐（干净桌面）
    bool dblClickCleanMode = true;
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
