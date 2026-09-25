#pragma once

#include <functional>

namespace desktopsticker::resmon {

// 纯 Win32 顶层窗口（刻意不用 WinUI/XAML）：
// WebView2 本身就是 HWND 宿主，用 Win32 可避开 AGENTS.md 记载的
// "点标题栏 X 会销毁 XAML Window、必须整体重建" 那套次级窗口脆弱点。
class ResMonWindow {
public:
    using SizeHandler = std::function<void(int, int)>;
    using DestroyHandler = std::function<void()>;
    // 自定义消息（>= WM_APP）回调：返回 true 表示已处理。
    // 供模块把工作线程的异步结果安全地切回 UI 线程（WebView2 只能在 UI 线程使用）。
    using AppMessageHandler = std::function<bool(UINT, WPARAM, LPARAM)>;

    ~ResMonWindow();

    bool Create();
    void Destroy();
    bool Exists() const { return hwnd_ != nullptr; }
    HWND Handle() const { return hwnd_; }
    void ShowAndFocus();

    void SetSizeHandler(SizeHandler h) { onSize_ = std::move(h); }
    // WM_DESTROY 前调用：供宿主释放 WebView2 控制器
    void SetDestroyHandler(DestroyHandler h) { onDestroy_ = std::move(h); }
    void SetAppMessageHandler(AppMessageHandler h) { onAppMessage_ = std::move(h); }

    int ClientWidth() const { return clientW_; }
    int ClientHeight() const { return clientH_; }

    // 供"双击桌面空白"等既有逻辑排除自身
    static bool IsResMonWindow(HWND hwnd);

    // 窗口过程：类注册时需要一个可取的函数指针，故为 public（实现细节）
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void notify_size();

    HWND hwnd_ = nullptr;
    int clientW_ = 0;
    int clientH_ = 0;
    SizeHandler onSize_;
    DestroyHandler onDestroy_;
    AppMessageHandler onAppMessage_;
};

} // namespace desktopsticker::resmon
