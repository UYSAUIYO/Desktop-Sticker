#include "pch.h"
#include "FeatureModule.h"

#include "desktopsticker/KnownFolders.h"
#include "desktopsticker/Log.h"
#include "desktopsticker/ShellLauncher.h"

namespace desktopsticker {

FeatureModule::FeatureModule() = default;

FeatureModule::~FeatureModule() {
    Shutdown();
}

bool FeatureModule::Init(const FeatureEvents& events) {
    events_ = events;

    try {
        // 数据根目录：%APPDATA%\DesktopSticker；解析失败退回临时目录（功能可用但不持久）
        std::filesystem::path root = std::filesystem::temp_directory_path() / L"DesktopSticker";
        const std::wstring appData = GetKnownPath(FOLDERID_RoamingAppData);
        if (!appData.empty()) root = std::filesystem::path(appData) / L"DesktopSticker";

        config_ = std::make_unique<ConfigStore>(root);
        config_->Load();

        index_ = std::make_unique<IndexService>(config_.get());
        index_->Rebuild();

        hotkey_ = std::make_unique<HotkeyService>();
        hotkey_->SetOnDoublePress([this]() {
            if (events_.hotkeyTriggered) events_.hotkeyTriggered();
        });
        // 应用配置的热键方案（双击空格 / 自定义组合键）
        hotkey_->SetHotkeyMode(config_->GetConfig().hotkeyMode, config_->GetConfig().customHotkey);

        iconService_ = std::make_unique<IconService>();
        workspace_ = std::make_unique<DesktopWorkspace>(config_.get(), iconService_.get(), [this]() {
            if (events_.zonesChanged) events_.zonesChanged();
        });
    } catch (const std::exception& e) {
        dstklog::Write(L"module", std::wstring(L"Init exception: ") +
                                       std::wstring(e.what(), e.what() + strlen(e.what())));
        return false;
    } catch (...) {
        dstklog::Write(L"module", L"Init unknown exception");
        return false;
    }

    initialized_ = true;
    return true;
}

bool FeatureModule::Start() {
    if (!initialized_) return false;
    hotkey_->SetEnabled(true);
    hotkey_->Start();
    if (!workspace_->Initialize()) {
        dstklog::Write(L"module", L"workspace Initialize FAILED (zones unavailable)");
        return false;
    }
    return true;
}

void FeatureModule::Stop() {
    if (!initialized_) return;
    hotkey_->Stop();
    workspace_->Shutdown();
}

void FeatureModule::Shutdown() {
    Stop();
    workspace_.reset(); // 先于 iconService_ 析构：分区窗口持有 IconService 裸指针
    iconService_.reset();
    hotkey_.reset();
    index_.reset();
    config_.reset();
    initialized_ = false;
}

std::vector<SearchResult> FeatureModule::Search(const std::wstring& query, size_t maxResults) {
    std::vector<SearchResult> results;
    if (!index_) return results;
    for (const auto& item : index_->Search(query, maxResults)) {
        SearchResult r;
        r.name = item.name;
        r.path = item.path;
        r.source = item.source;
        r.isApp = item.isApp;
        results.push_back(std::move(r));
    }
    return results;
}

bool FeatureModule::AddApp(const std::wstring& path) {
    if (!index_) return false;
    const bool ok = index_->AddApp(path);
    if (ok) index_->Rebuild();
    return ok;
}

bool FeatureModule::RemoveApp(const std::wstring& path) {
    if (!index_) return false;
    const bool ok = index_->RemoveApp(path);
    if (ok) index_->Rebuild();
    return ok;
}

std::vector<std::wstring> FeatureModule::GetApps() {
    if (!index_) return {};
    return index_->Apps();
}

AppConfig FeatureModule::GetConfig() {
    if (!config_) return AppConfig{};
    return config_->GetConfig();
}

void FeatureModule::SetConfig(const AppConfig& config) {
    if (!config_) return;
    const AppConfig old = config_->GetConfig();
    config_->SetConfig(config);
    config_->Save();
    if (hotkey_) hotkey_->SetHotkeyMode(config.hotkeyMode, config.customHotkey);
    if (workspace_) workspace_->SetZoneSpacing(config.zoneColumnSpacing, config.zoneRowSpacing);
    // 列容量变化：按新容量重排初始布局（覆盖手动拖放位置属预期行为）
    if (workspace_ && old.zoneColumnCards != config.zoneColumnCards) {
        workspace_->RelayoutZones();
    }
    // 桌面时钟开关
    if (workspace_ && old.showClock != config.showClock) {
        workspace_->SetClockVisible(config.showClock);
    }
    // 只有搜索范围相关变化才重建索引：设置页拖动间距/换主题不应触发全盘扫描
    const bool searchChanged = old.searchDesktop != config.searchDesktop ||
                               old.searchKnownFolders != config.searchKnownFolders ||
                               old.searchStartMenu != config.searchStartMenu ||
                               old.includeHiddenFiles != config.includeHiddenFiles;
    if (searchChanged && index_) index_->Rebuild();
}

void FeatureModule::OpenItem(const std::wstring& path) {
    ShellLauncher::Open(path);
}

void FeatureModule::RestoreDesktop() {
    if (workspace_) workspace_->RestoreDesktop();
}

bool FeatureModule::TilesHidden() {
    return workspace_ && workspace_->IsCleanMode();
}

void FeatureModule::SetTilesHidden(bool hidden) {
    if (workspace_) workspace_->SetCleanMode(hidden);
}

HICON FeatureModule::GetIcon(const std::wstring& path, int size) {
    return iconService_ ? iconService_->GetIcon(path, size) : nullptr;
}

} // namespace desktopsticker
