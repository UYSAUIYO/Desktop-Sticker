#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AudioEngine.h"
#include "D3dContext.h"
#include "WallPaperWindow.h"
#include "WallpaperBackend.h"
#include "desktopsticker/wallpaper/ClockPolicy.h"
#include "desktopsticker/wallpaper/PresentationArbiter.h"

namespace desktopsticker::wallpaper {

// 专用渲染线程：窗口、D3D/DComp、后端与帧调度全部在这一条线程上完成，
// 不触碰宿主 UI 线程的窗口与布局状态。
//
// 后端请求由别的线程登记，渲染线程负责创建/打开（解码器绝不跨线程使用）。
class FrameSchedulerLoop {
public:
    ~FrameSchedulerLoop();

    // 阻塞直到线程完成初始化；返回 false 表示窗口或 D3D 初始化失败
    bool Start();
    void Stop();
    bool Running() const { return running_.load(); }

    // 必须在 Start 之前设置（模块持有 AudioEngine）
    void SetAudioEngine(AudioEngine* audio) { audio_ = audio; }
    void SetPaths(std::wstring exeDir, std::wstring libraryRoot) {
        exeDir_ = std::move(exeDir);
        libraryRoot_ = std::move(libraryRoot);
    }

    // 切换后端（线程安全）。渲染线程会在下一轮打开它。
    void SetBackendRequest(const BackendRequest& request);
    void ClearBackend();
    void SetSpeed(double speed);

    void SetPaused(bool paused);
    bool Paused() const { return paused_.load(); }

    HWND Window() const { return hwnd_; }
    bool Embedded() const { return window_.Embedded(); }
    // 分区层级变化后重申壁纸窗口的底部位置
    void ReassertBottom() { window_.PlaceAtBottom(); }
    const char* LastError() const { return d3d_.LastError(); }

    // ---- 播放回显（设置页显示"当前方式 + 实时帧率"）----
    // 渲染线程每秒更新一次；读取侧不做同步也不会读到半个值
    double MeasuredFps() const { return measuredFps_.load(); }
    int LastFrameWidth() const { return lastFrameW_.load(); }
    int LastFrameHeight() const { return lastFrameH_.load(); }
    bool Playing() const { return playing_.load(); }
    std::string BackendName() const;

private:
    void thread_main(std::promise<bool> init);
    void pump_messages(bool& quit);
    // 渲染线程：消费待处理的后端请求
    void apply_pending_backend();

    WallPaperWindow window_;
    D3dContext d3d_;
    std::thread thread_;
    DWORD threadId_ = 0;
    HWND hwnd_ = nullptr;

    std::atomic<bool> quit_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> speed_{1.0};
    AudioEngine* audio_ = nullptr;
    std::wstring exeDir_;
    std::wstring libraryRoot_;

    std::mutex requestMutex_;
    BackendRequest pendingRequest_;
    bool hasPendingRequest_ = false;
    bool clearRequested_ = false;

    // 仅渲染线程访问
    std::unique_ptr<IWallpaperBackend> backend_;
    PresentationArbiter arbiter_;
    bool lastPaused_ = false;
    double lastSpeed_ = 1.0;
    ClockMaster lastMaster_ = ClockMaster::Qpc;
    std::vector<uint8_t> frameBuffer_;

    // 播放回显：渲染线程每秒写一次，设置页随时读（原子量，读到的是完整值）
    std::atomic<double> measuredFps_{0.0};
    std::atomic<int> lastFrameW_{0};
    std::atomic<int> lastFrameH_{0};
    std::atomic<bool> playing_{false};
    mutable std::mutex backendNameMutex_;
    std::string backendName_{"-"};
};

} // namespace desktopsticker::wallpaper
