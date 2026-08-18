#pragma once
#include <functional>
#include <string>
#include <vector>

#include "desktopsticker/ConfigStore.h"

namespace desktopsticker {

struct FeatureEvents {
    // 双击空格（或其他配置热键）触发
    std::function<void()> hotkeyTriggered;
    // 索引重建完成
    std::function<void()> indexUpdated;
    // 分区布局变化（用于通知 UI 刷新）
    std::function<void()> zonesChanged;
};

struct SearchResult {
    std::wstring name;
    std::wstring path;
    std::wstring source;
    bool isApp = false;
};

class IFeatureModule {
public:
    virtual ~IFeatureModule() = default;
    virtual bool Init(const FeatureEvents& events) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Shutdown() = 0;

    virtual std::vector<SearchResult> Search(const std::wstring& query, size_t maxResults) = 0;
    virtual bool AddApp(const std::wstring& path) = 0;
    virtual bool RemoveApp(const std::wstring& path) = 0;
    virtual std::vector<std::wstring> GetApps() = 0;
    virtual AppConfig GetConfig() = 0;
    virtual void SetConfig(const AppConfig& config) = 0;
    virtual void OpenItem(const std::wstring& path) = 0;
    virtual void RestoreDesktop() = 0;
};

} // namespace desktopsticker

extern "C" __declspec(dllexport) desktopsticker::IFeatureModule* CreateFeatureModule();
extern "C" __declspec(dllexport) void DestroyFeatureModule(desktopsticker::IFeatureModule* module);
