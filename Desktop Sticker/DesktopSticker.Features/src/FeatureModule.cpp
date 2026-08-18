#include "pch.h"
#include "FeatureModule.h"

namespace desktopsticker {

bool FeatureModule::Init(const FeatureEvents& events) {
    events_ = events;
    initialized_ = true;
    return true;
}

void FeatureModule::Start() {
    if (!initialized_) return;
    // 后续 Task 在这里启动 HotkeyService / IndexService / DesktopWorkspace
}

void FeatureModule::Stop() {
    // 后续 Task 在这里停止服务
}

void FeatureModule::Shutdown() {
    Stop();
    initialized_ = false;
}

} // namespace desktopsticker
