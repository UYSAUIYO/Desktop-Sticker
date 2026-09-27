#pragma once

// .dswall 场景壁纸的内嵌 Godot 引擎宿主（进程级单例）。
//
// 设计约束（libgodot 的限制）：
//   - Godot 实例创建后不可销毁重建（引擎不支持），所以实例只在首次使用时创建，
//     切换壁纸包走 dstk_godot_load_pack（运行中挂载新 PCK + 切主场景）；
//   - 呈现窗口常驻（WorkerW 子窗口），激活时显示、停用时隐藏，绝不销毁；
//   - 全部方法只在渲染线程调用（与 IWallpaperBackend 契约一致，shutdown 除外）。

#include <windows.h>

#include <string>

namespace desktopsticker::wallpaper {

class GodotEngineHost {
public:
    static GodotEngineHost& instance();

    // 运行库 DLL 是否就位（<exeDir>\godot\ 下）。
    bool runtime_available() const;

    // 激活一个壁纸包：首次创建引擎/窗口，之后按需切换包。parent 为 WorkerW（可为空）。
    // 返回 false 时 error 里是原因（中文），调用方据此保持上一画面。
    bool activate(const std::wstring& packPath, const std::string& mainScene,
                  HWND parent, int width, int height, std::string& error);

    // 停止迭代并隐藏窗口（引擎保留，供下次激活/切包）。
    void deactivate();

    // 驱动一帧（自呈现型后端每轮调用）。
    void tick();

    // 暂停/恢复：向引擎发应用级通知（真正冻结靠调用方停止 tick）。
    void notify_paused(bool paused);

    bool active() const { return active_; }

    // 进程退出前调用：销毁实例与窗口，恢复桌面壁纸（Explorer 的壁纸 WorkerW 空置会露白）。
    void shutdown();

    GodotEngineHost(const GodotEngineHost&) = delete;
    GodotEngineHost& operator=(const GodotEngineHost&) = delete;

private:
    GodotEngineHost() = default;
    ~GodotEngineHost() = default;

    bool ensure_runtime(std::string& error);
    bool ensure_window(HWND parent, int width, int height, std::string& error);
    bool ensure_instance(const std::wstring& packPath, const std::string& mainScene, std::string& error);
    void destroy_all();

    HMODULE dll_ = nullptr;
    HWND window_ = nullptr;
    HWND parent_ = nullptr;      // 非空 = 已挂进 WorkerW（shutdown 时要做壁纸恢复）
    void* instance_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool active_ = false;
    bool notified_paused_ = false;
    std::wstring currentPack_;
    std::string currentScene_;
    std::string lastError_;

    void (*fn_set_present_)(void*) = nullptr;
    void* (*fn_create_)(int, char**) = nullptr;
    bool (*fn_start_)(void*) = nullptr;
    bool (*fn_iteration_)(void*) = nullptr;
    void (*fn_pause_)(void*) = nullptr;
    void (*fn_resume_)(void*) = nullptr;
    void (*fn_destroy_)(void*) = nullptr;
    bool (*fn_load_pack_)(void*, const char*, const char*) = nullptr;
};

} // namespace desktopsticker::wallpaper
