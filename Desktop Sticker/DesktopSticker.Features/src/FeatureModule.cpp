#include "pch.h"
#include "FeatureModule.h"

#include "desktopsticker/ShellLauncher.h"

namespace desktopsticker {

FeatureModule::FeatureModule() = default;

FeatureModule::~FeatureModule() {
    Shutdown();
}

bool FeatureModule::Init(const FeatureEvents& events) {
    events_ = events;

    PWSTR appData = nullptr;
    std::filesystem::path root = std::filesystem::temp_directory_path() / L"DesktopSticker";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        root = std::filesystem::path(appData) / L"DesktopSticker";
        CoTaskMemFree(appData);
    }

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

    workspace_ = std::make_unique<DesktopWorkspace>(config_.get(), [this]() {
        if (events_.zonesChanged) events_.zonesChanged();
    });

    initialized_ = true;
    return true;
}

void FeatureModule::Start() {
    if (!initialized_) return;
    hotkey_->SetEnabled(true);
    hotkey_->Start();
    workspace_->Initialize();
}

void FeatureModule::Stop() {
    if (!initialized_) return;
    hotkey_->Stop();
    workspace_->Shutdown();
}

void FeatureModule::Shutdown() {
    Stop();
    workspace_.reset();
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
    config_->SetConfig(config);
    config_->Save();
    if (hotkey_) hotkey_->SetHotkeyMode(config.hotkeyMode, config.customHotkey);
    if (workspace_) workspace_->SetZoneSpacing(config.zoneColumnSpacing, config.zoneRowSpacing);
    if (index_) index_->Rebuild();
}

void FeatureModule::OpenItem(const std::wstring& path) {
    ShellLauncher::Open(path);
}

void FeatureModule::RestoreDesktop() {
    if (workspace_) workspace_->RestoreDesktop();
}

} // namespace desktopsticker
