#pragma once

// EXE ↔ ResMon 的唯一接口边界。与 IFeatureModule / IWallPaperModule 同构：
// EXE 只消费本头文件，不消费该 DLL 的任何其他头文件。
// 本头文件不得 include pch.h / WebView2.h / nlohmann，必须能被 EXE 与单测工程独立包含。

#include <string>

#include "desktopsticker/ResMonExport.h"

namespace desktopsticker {

// 由 EXE 注入，避免 DLL 之间相互依赖
struct ResMonPaths {
    std::wstring exeDir;         // 负载 / 资源 / PDB 所在目录
    std::wstring configDir;      // %APPDATA%\DesktopSticker
    std::wstring wallpaperRoot;  // 取自 IWallPaperModule::GetSettings().libraryRoot，可为空
};

class IResMonModule {
public:
    virtual ~IResMonModule() = default;

    // 创建 WebView2 环境与宿主窗口（窗口初始不可见）。
    // 失败返回 false，但模块实例仍保留，供 Available() 查询后把菜单项置灰。
    virtual bool Init(const ResMonPaths& paths) = 0;
    virtual bool Show() = 0;      // 显示并前置窗口；窗口已存在则只前置
    virtual void Shutdown() = 0;
    virtual bool Available() = 0; // 环境与窗口是否就绪
};

} // namespace desktopsticker

extern "C" DESKTOPSTICKER_RESMON_API desktopsticker::IResMonModule* CreateResMonModule();
extern "C" DESKTOPSTICKER_RESMON_API void DestroyResMonModule(desktopsticker::IResMonModule*);
