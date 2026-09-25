#pragma once
#include <functional>
#include <memory>
#include <string>

#include "desktopsticker/IFeatureModule.h"
#include "desktopsticker/IWallPaperModule.h"

namespace desktopsticker::app {

class Host {
public:
    Host();
    ~Host();

    bool LoadFeatures();
    void UnloadFeatures();
    // 壁纸是可选组件：失败只降级为不可用，绝不影响分区/搜索/时钟
    bool LoadWallPaper();
    void UnloadWallPaper();
    bool Start();
    void Stop();

    void SetHotkeyCallback(std::function<void()> cb) { hotkeyCallback_ = std::move(cb); }
    desktopsticker::IFeatureModule* Module() const { return module_; }
    // 可能为 nullptr（DLL 缺失或初始化失败）
    desktopsticker::IWallPaperModule* WallPaper() const { return wpModule_; }
    void SetWallPaperEvents(std::function<void()> libraryChanged,
                            std::function<void()> playbackStateChanged);

private:
    HMODULE dll_ = nullptr;
    desktopsticker::IFeatureModule* module_ = nullptr;
    std::function<void()> hotkeyCallback_;
    std::wstring dllPath_;

    HMODULE wpDll_ = nullptr;
    desktopsticker::IWallPaperModule* wpModule_ = nullptr;
    std::function<void()> wpLibraryChanged_;
    std::function<void()> wpPlaybackChanged_;
};

} // namespace desktopsticker::app
