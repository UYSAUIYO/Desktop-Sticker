#pragma once

// EXE ↔ WallPaper 的唯一接口边界。
// 与 IFeatureModule 同构：EXE 只消费本头文件，不消费该 DLL 的任何其他头文件。
// 接口即 ABI：任何字段/虚函数变更都是 ABI 断裂，两端必须由同一份源码同步重编。
//
// 本头文件不得 include pch.h 或任何 D3D / MF 头，必须能被 EXE 与单测工程独立包含。

#include <windows.h> // HICON

#include <functional>
#include <string>
#include <vector>

#include "desktopsticker/WallPaperExport.h"
#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker {

struct WallPaperEvents {
    // 注意：这两个回调可能在**后台线程**被调用（库变更由缩略图/转码线程触发，
    // 播放状态由监控线程触发）。宿主必须自行切回 UI 线程后再触碰界面对象。
    std::function<void()> libraryChanged;
    std::function<void()> playbackStateChanged;
};

class IWallPaperModule {
public:
    virtual ~IWallPaperModule() = default;

    virtual bool Init(const WallPaperEvents& events) = 0;
    // 返回 false 表示桌面嵌入等初始化异常；宿主应降级为"壁纸不可用"而不是静默
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void Shutdown() = 0;

    // ---- 媒体库 ----
    virtual std::vector<WallPaperItem> ListItems() = 0;
    virtual bool Import(const std::wstring& srcPath, std::wstring& outId) = 0;
    virtual bool Rename(const std::wstring& id, const std::wstring& name) = 0;
    // 只删库内副本，绝不触碰用户原始文件
    virtual bool Remove(const std::wstring& id) = 0;
    // 返回缓存持有的 HICON，调用方不得 DestroyIcon
    virtual HICON GetThumbnail(const std::wstring& id, int size) = 0;

    // ---- 设置与播放 ----
    virtual WallPaperSettings GetSettings() = 0;
    virtual bool SetSettings(const WallPaperSettings& settings) = 0;
    virtual bool RegenerateVariant(const std::wstring& id, VariantKind kind) = 0;
    // 手动更换存储位置：复制 + 校验 + 切换记录，旧位置保留不删
    virtual bool ChangeLibraryRoot(const std::wstring& newRoot) = 0;
    virtual void SetUserPaused(bool paused) = 0;
    virtual bool IsUserPaused() = 0;
    // 模块是否可用（库根校验通过、DLL 依赖齐备等）。不可用时设置页应禁用控件
    virtual bool Available() = 0;
};

} // namespace desktopsticker

extern "C" DESKTOPSTICKER_WALLPAPER_API desktopsticker::IWallPaperModule* CreateWallPaperModule();
extern "C" DESKTOPSTICKER_WALLPAPER_API void DestroyWallPaperModule(desktopsticker::IWallPaperModule*);
