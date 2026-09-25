#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "D3dContext.h"
#include "WallPaperWindow.h"

namespace desktopsticker::wallpaper {

// 专用渲染线程：窗口、D3D/DComp、解码调度全部在这一条线程上完成，
// 不触碰宿主 UI 线程的窗口与布局状态。
class FrameSchedulerLoop {
public:
    // 由调用方提供下一帧 BGRA；返回 false 表示本轮无帧（暂停/回卷/失败）
    using FrameProvider = std::function<bool(std::vector<uint8_t>&, int&, int&)>;

    ~FrameSchedulerLoop();

    // 阻塞直到线程完成初始化；返回 false 表示窗口或 D3D 初始化失败
    bool Start();
    void Stop();
    bool Running() const { return running_.load(); }

    void SetProvider(FrameProvider provider);
    void SetPaused(bool paused);
    bool Paused() const { return paused_.load(); }
    void SetFpsHint(double fps) { fpsHint_.store(fps); }

    HWND Window() const { return hwnd_; }
    bool Embedded() const { return window_.Embedded(); }
    // 分区层级变化后重申壁纸窗口的底部位置
    void ReassertBottom() { window_.PlaceAtBottom(); }
    const char* LastError() const { return d3d_.LastError(); }

private:
    void thread_main(std::promise<bool> init);
    void pump_messages(bool& quit);

    WallPaperWindow window_;
    D3dContext d3d_;
    std::thread thread_;
    DWORD threadId_ = 0;
    HWND hwnd_ = nullptr;

    std::atomic<bool> quit_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> fpsHint_{30.0};

    std::mutex providerMutex_;
    FrameProvider provider_;
    std::vector<uint8_t> frameBuffer_;
};

} // namespace desktopsticker::wallpaper
