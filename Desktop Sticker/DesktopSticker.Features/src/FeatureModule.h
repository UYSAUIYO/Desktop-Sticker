#pragma once
#include <memory>
#include <string>
#include <vector>

#include "desktopsticker/IFeatureModule.h"
#include "desktopsticker/ConfigStore.h"
#include "desktopsticker/DesktopWorkspace.h"
#include "desktopsticker/HotkeyService.h"
#include "desktopsticker/IndexService.h"

namespace desktopsticker {

class FeatureModule final : public IFeatureModule {
public:
    FeatureModule();
    ~FeatureModule() override;

    bool Init(const FeatureEvents& events) override;
    bool Start() override;
    void Stop() override;
    void Shutdown() override;

    std::vector<SearchResult> Search(const std::wstring& query, size_t maxResults) override;
    bool AddApp(const std::wstring& path) override;
    bool RemoveApp(const std::wstring& path) override;
    std::vector<std::wstring> GetApps() override;
    AppConfig GetConfig() override;
    void SetConfig(const AppConfig& config) override;
    void OpenItem(const std::wstring& path) override;
    void RestoreDesktop() override;

private:
    FeatureEvents events_;
    bool initialized_ = false;

    std::unique_ptr<ConfigStore> config_;
    std::unique_ptr<IndexService> index_;
    std::unique_ptr<HotkeyService> hotkey_;
    std::unique_ptr<DesktopWorkspace> workspace_;
};

} // namespace desktopsticker
