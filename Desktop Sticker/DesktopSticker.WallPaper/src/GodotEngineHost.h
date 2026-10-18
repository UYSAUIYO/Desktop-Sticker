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

#include "MouseRelay.h"

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

    // 鼠标交互：把一条转发事件以窗口消息的形式 PostMessage 给引擎窗口。
    // 引擎的窗口过程（fork 装的那层薄壳）走它自己的原生输入处理，和真实窗口同一条
    // 路径；窗口过程在按下后会立刻把系统捕获交还，所以引擎的鼠标流始终只有
    // "宿主转发"这一条，不会和真实消息叠在一起。
    bool send_mouse(const MouseRelayEvent& event);

    // 键盘交互：把一条按键 PostMessage 给引擎窗口（钩子转发的那条路）。
    //
    // 为什么不是只靠焦点：壁纸窗口挂在 explorer 的 WorkerW 下，SetFocus 会被系统
    // 拒绝（本机实测返回 87 / ERROR_INVALID_PARAMETER），原生键盘流到不了壁纸。
    // 所以键盘和鼠标一样由低级钩子转发 —— 引擎的薄壳窗口过程把 WM_KEYDOWN/UP
    // 交给 DisplayServerWindows::WndProc，修饰键与扫描码由 Windows 自己解释，
    // 和真实窗口收到的按键完全同一条处理路径。窗口是否持有焦点无所谓。
    bool post_key(int vk, int scan, bool sys, bool extended, bool down, bool repeat = false);

    // 焦点移交（能拿就拿的补充路径；失败不影响键盘，见 post_key）。
    void focus_keyboard();

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
