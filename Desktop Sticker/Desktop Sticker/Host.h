#pragma once
#include <functional>
#include <memory>
#include <string>

#include "desktopsticker/IFeatureModule.h"

namespace desktopsticker::app {

class Host {
public:
    Host();
    ~Host();

    bool LoadFeatures();
    void UnloadFeatures();
    bool Start();
    void Stop();

    void SetHotkeyCallback(std::function<void()> cb) { hotkeyCallback_ = std::move(cb); }
    desktopsticker::IFeatureModule* Module() const { return module_; }

private:
    HMODULE dll_ = nullptr;
    desktopsticker::IFeatureModule* module_ = nullptr;
    std::function<void()> hotkeyCallback_;
    std::wstring dllPath_;
};

} // namespace desktopsticker::app
