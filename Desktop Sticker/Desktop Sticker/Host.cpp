#include "pch.h"
#include "Host.h"

#include "AppLog.h"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace desktopsticker::app {

Host::Host() {
    dllPath_ = fs::path(L".") / L"DesktopSticker.Features.dll";
}

Host::~Host() {
    UnloadWallPaper();
    UnloadFeatures();
}

void Host::SetWallPaperEvents(std::function<void()> libraryChanged,
                              std::function<void()> playbackStateChanged) {
    wpLibraryChanged_ = std::move(libraryChanged);
    wpPlaybackChanged_ = std::move(playbackStateChanged);
}

bool Host::LoadWallPaper() {
    if (wpModule_) return true;

    wchar_t exeDir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    const fs::path dll = fs::path(exeDir).parent_path() / L"DesktopSticker.WallPaper.dll";
    if (!fs::exists(dll)) {
        desktopsticker::app::AppLog("wallpaper", "DesktopSticker.WallPaper.dll not found; feature disabled");
        return false;
    }

    wpDll_ = LoadLibraryW(dll.c_str());
    if (!wpDll_) {
        desktopsticker::app::AppLog("wallpaper", "LoadLibraryW failed; feature disabled");
        return false;
    }

    using CreateFn = desktopsticker::IWallPaperModule* (*)();
    using DestroyFn = void (*)(desktopsticker::IWallPaperModule*);

    auto create = reinterpret_cast<CreateFn>(GetProcAddress(wpDll_, "CreateWallPaperModule"));
    auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(wpDll_, "DestroyWallPaperModule"));
    if (!create || !destroy) {
        FreeLibrary(wpDll_);
        wpDll_ = nullptr;
        desktopsticker::app::AppLog("wallpaper", "exports missing; feature disabled");
        return false;
    }

    desktopsticker::WallPaperEvents events;
    events.libraryChanged = [this]() { if (wpLibraryChanged_) wpLibraryChanged_(); };
    events.playbackStateChanged = [this]() { if (wpPlaybackChanged_) wpPlaybackChanged_(); };

    // 与 LoadFeatures 同规矩：任何失败都收干净资源并返回 false，异常不得穿透
    try {
        wpModule_ = create();
        if (!wpModule_) throw std::runtime_error("CreateWallPaperModule returned null");
        if (!wpModule_->Init(events)) throw std::runtime_error("Init failed");
        if (!wpModule_->Start()) {
            // Start 失败只表示壁纸不可用（库根校验失败、桌面嵌入异常等），宿主继续运行
            desktopsticker::app::AppLog("wallpaper", "Start failed; feature degraded");
        }
    } catch (...) {
        if (wpModule_) {
            if (destroy) destroy(wpModule_);
            wpModule_ = nullptr;
        }
        FreeLibrary(wpDll_);
        wpDll_ = nullptr;
        desktopsticker::app::AppLog("wallpaper", "init threw; feature disabled");
        return false;
    }
    return true;
}

void Host::UnloadWallPaper() {
    if (wpModule_) {
        wpModule_->Shutdown();
        using DestroyFn = void (*)(desktopsticker::IWallPaperModule*);
        auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(wpDll_, "DestroyWallPaperModule"));
        if (destroy) destroy(wpModule_);
        wpModule_ = nullptr;
    }
    if (wpDll_) {
        FreeLibrary(wpDll_);
        wpDll_ = nullptr;
    }
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
    if (wpModule_) wpModule_->Stop();
}

} // namespace desktopsticker::app
