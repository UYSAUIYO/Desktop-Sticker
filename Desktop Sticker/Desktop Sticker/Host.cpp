#include "pch.h"
#include "Host.h"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace desktopsticker::app {

Host::Host() {
    dllPath_ = fs::path(L".") / L"DesktopSticker.Features.dll";
}

Host::~Host() {
    UnloadFeatures();
}

bool Host::LoadFeatures() {
    if (module_) return true;

    wchar_t exeDir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    fs::path dll = fs::path(exeDir).parent_path() / L"DesktopSticker.Features.dll";
    if (!fs::exists(dll)) dll = dllPath_;

    dll_ = LoadLibraryW(dll.c_str());
    if (!dll_) return false;

    using CreateFn = desktopsticker::IFeatureModule* (*)();
    using DestroyFn = void (*)(desktopsticker::IFeatureModule*);

    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll_, "CreateFeatureModule"));
    auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(dll_, "DestroyFeatureModule"));
    if (!create || !destroy) {
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }

    // DLL 内 new/初始化可能抛异常：任何失败都必须收干净资源并返回 false，
    // 由调用方（App）向用户提示，不允许异常穿透到 OnLaunched
    desktopsticker::FeatureEvents events;
    events.hotkeyTriggered = [this]() {
        if (hotkeyCallback_) hotkeyCallback_();
    };
    try {
        module_ = create();
        if (!module_) throw std::runtime_error("CreateFeatureModule returned null");
        if (!module_->Init(events)) throw std::runtime_error("Init failed");
    } catch (...) {
        if (module_) {
            if (destroy) destroy(module_);
            module_ = nullptr;
        }
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }
    return true;
}

void Host::UnloadFeatures() {
    if (module_) {
        module_->Shutdown();
        using DestroyFn = void (*)(desktopsticker::IFeatureModule*);
        auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(dll_, "DestroyFeatureModule"));
        if (destroy) destroy(module_);
        module_ = nullptr;
    }
    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
}

bool Host::Start() {
    if (!module_) return false;
    return module_->Start(); // 失败说明桌面分区初始化异常，App 会向用户提示
}

void Host::Stop() {
    if (module_) module_->Stop();
}

} // namespace desktopsticker::app
