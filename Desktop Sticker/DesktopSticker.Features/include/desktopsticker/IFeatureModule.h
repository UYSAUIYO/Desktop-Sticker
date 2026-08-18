#pragma once
#include <functional>

namespace desktopsticker {

struct FeatureEvents {
    // 双击空格（或其他配置热键）触发
    std::function<void()> hotkeyTriggered;
    // 索引重建完成
    std::function<void()> indexUpdated;
    // 分区布局变化（用于通知 UI 刷新）
    std::function<void()> zonesChanged;
};

class IFeatureModule {
public:
    virtual ~IFeatureModule() = default;
    virtual bool Init(const FeatureEvents& events) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Shutdown() = 0;
};

} // namespace desktopsticker

extern "C" __declspec(dllexport) desktopsticker::IFeatureModule* CreateFeatureModule();
extern "C" __declspec(dllexport) void DestroyFeatureModule(desktopsticker::IFeatureModule* module);
