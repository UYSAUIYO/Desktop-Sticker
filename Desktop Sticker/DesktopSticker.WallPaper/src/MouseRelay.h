#pragma once

// 低级鼠标钩子 + 桌面命中判断 + 事件排队（OS 层）。
//
// 只有"场景 / 网页"这类想接交互的后端才会 Attach；视频/图片壁纸不装钩子，
// 桌面行为保持原样。
//
// 线程模型：
//   · Attach / Detach / Drain / SetEnabled 只在渲染线程调用；
//   · 钩子线程（本类自建）只做命中判断与入队，绝不碰后端对象；
//   · 真正把事件交给后端发生在 Drain()，也就是渲染线程。

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

#include "desktopsticker/wallpaper/MouseRelay.h"

namespace desktopsticker::wallpaper {

// 一条转发事件：壁纸窗口客户区像素 + 屏幕像素 + 当时的按钮/修饰键状态。
struct MouseRelayEvent {
    RelayEventKind kind = RelayEventKind::Move;
    int x = 0;        // 壁纸窗口客户区
    int y = 0;
    int screenX = 0;  // 屏幕坐标（场景后端据此映射到引擎自己的窗口）
    int screenY = 0;
    int relX = 0;     // 相对上一条转发事件的位移
    int relY = 0;
    int wheelDelta = 0; // ±120 的倍数（水平滚轮用同一字段，正右负左）
    int buttons = 0;    // 事件发生时按住的按钮（kRelayLeft/Right/Middle 位）
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// 接收方（WebView2 / 内嵌 Godot）。只在渲染线程上被调用。
class IMouseSink {
public:
    virtual ~IMouseSink() = default;
    virtual void OnRelayMouse(const MouseRelayEvent& event) = 0;

    // 后端自己是否已经握着系统鼠标捕获（按下期间）。为 true 时系统会把真实鼠标消息
    // 直接投递给它，宿主必须停止转发与拦截，否则会和真实消息重复。
    // 只有"能用窗口消息收输入"的后端（内嵌 Godot）会返回 true；
    // WebView2 视觉宿主只能靠 SendMouseInput，永远返回 false。
    virtual bool OwnsCapture() const { return false; }
};

class MouseRelay {
public:
    MouseRelay() = default;
    ~MouseRelay();

    MouseRelay(const MouseRelay&) = delete;
    MouseRelay& operator=(const MouseRelay&) = delete;

    // 用户设置（默认开）。关掉后钩子立刻不再拦截任何事件。
    void SetEnabled(bool enabled) { enabled_.store(enabled); }
    bool Enabled() const { return enabled_.load(); }

    // 后端打开：登记交互窗口并启动钩子线程（渲染线程）。
    void Attach(HWND window, IMouseSink* sink);
    // 后端关闭：撤销登记并停掉钩子线程（渲染线程）。
    void Detach(IMouseSink* sink);
    bool Attached() const { return sink_ != nullptr; }

    // 渲染线程每轮调用：排空队列并把事件交给 sink。paused = 暂停中（不转发）。
    void Drain(bool paused);

private:
    static LRESULT CALLBACK hook_proc(int code, WPARAM wparam, LPARAM lparam);

    void start_thread();
    void stop_thread();
    void thread_main();
    void handle_mouse(WPARAM msg, const MSLLHOOKSTRUCT& info, bool& swallow);
    void classify(const POINT& pt, bool need_icon, RelayHit& hit);
    bool icon_at(const POINT& pt);
    HWND find_desktop_list_view();
    RelayViewport viewport() const;
    void push(MouseRelayEvent event);
    // 给后端补发"抬起"，并清空捕获状态（离开画面 / 暂停 / 关闭交互时）
    void release_all_buttons();
    void clear_queue();

    HWND window_ = nullptr;
    IMouseSink* sink_ = nullptr;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> active_{true};
    // 后端是否握着系统捕获（每帧由 Drain 在渲染线程上问一次）
    std::atomic<bool> nativeCapture_{false};

    std::thread thread_;
    DWORD thread_id_ = 0;
    HANDLE ready_event_ = nullptr;

    // 只有钩子线程访问
    RelayPolicy policy_;
    bool was_active_ = false;
    bool inside_ = false;         // 后端是否认为鼠标还在画面里（用于补 Leave）
    int lastScreenX_ = 0;
    int lastScreenY_ = 0;
    int lastClientX_ = 0;
    int lastClientY_ = 0;
    HWND list_view_ = nullptr;    // 桌面图标列表（LVM_HITTEST 用）
    // 诊断计数（5 秒一行日志）：转发 / 因命中被拒 / 离开 / 补发的抬起
    uint32_t statMoves_ = 0;
    uint32_t statButtons_ = 0;
    uint32_t statWheels_ = 0;
    uint32_t statLeaves_ = 0;
    uint32_t statRejected_ = 0;
    uint32_t statReleases_ = 0;
    DWORD lastStatTick_ = 0;

    std::mutex queue_mutex_;
    std::deque<MouseRelayEvent> queue_;
};

} // namespace desktopsticker::wallpaper
