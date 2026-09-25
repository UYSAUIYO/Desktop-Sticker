#include "pch.h"
#include "FrameSchedulerLoop.h"

#include "Log.h"
#include "desktopsticker/wallpaper/FrameAdvance.h"   // clamp_speed
#include "desktopsticker/wallpaper/FrameScheduler.h"

#include <future>

namespace desktopsticker::wallpaper {

namespace {

int64_t qpc_100ns() {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart * 10000000LL / freq;
}

} // namespace

FrameSchedulerLoop::~FrameSchedulerLoop() {
    Stop();
}

bool FrameSchedulerLoop::Start() {
    if (thread_.joinable()) return running_.load();

    quit_.store(false);
    std::promise<bool> init;
    auto ready = init.get_future();
    thread_ = std::thread([this, p = std::move(init)]() mutable {
        SetThreadDescription(GetCurrentThread(), L"壁纸渲染与帧调度");
        thread_main(std::move(p));
    });

    if (ready.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
        wp_log("FrameSchedulerLoop: init timed out");
        quit_.store(true);
        Stop();
        return false;
    }
    return ready.get();
}

void FrameSchedulerLoop::Stop() {
    if (!thread_.joinable()) return;
    quit_.store(true);
    if (threadId_) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    }
    thread_.join();
    threadId_ = 0;
}

void FrameSchedulerLoop::SetBackendRequest(const BackendRequest& request) {
    std::lock_guard<std::mutex> lock(requestMutex_);
    pendingRequest_ = request;
    hasPendingRequest_ = true;
    clearRequested_ = false;
}

void FrameSchedulerLoop::ClearBackend() {
    std::lock_guard<std::mutex> lock(requestMutex_);
    clearRequested_ = true;
    hasPendingRequest_ = false;
}

void FrameSchedulerLoop::SetSpeed(double speed) {
    speed_.store(clamp_speed(speed));
}

void FrameSchedulerLoop::SetPaused(bool paused) {
    paused_.store(paused);
}

void FrameSchedulerLoop::pump_messages(bool& quit) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            quit = true;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void FrameSchedulerLoop::apply_pending_backend() {
    BackendRequest request;
    bool has = false;
    bool clear = false;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        has = hasPendingRequest_;
        request = pendingRequest_;
        clear = clearRequested_;
        hasPendingRequest_ = false;
        clearRequested_ = false;
    }
    if (!has && !clear) return;

    // 先撤掉当前后端；若它曾是自呈现型，记下需要复位 DComp
    const bool wasSelfPresenting = backend_ && backend_->SelfPresenting();
    if (backend_) {
        backend_->Close();
        backend_.reset();
    }
    if (wasSelfPresenting) {
        const auto d = arbiter_.OnBackendClosed();
        if (d.change) d3d_.Resume();
    }
    lastPaused_ = false;   // 新后端要以当前状态重新同步一次

    if (clear || !has) return;

    BackendContext ctx;
    ctx.window = hwnd_;
    ctx.width = window_.Width();
    ctx.height = window_.Height();
    ctx.d3dDevice = d3d_.Device();
    ctx.exeDir = exeDir_;
    ctx.libraryRoot = libraryRoot_;
    ctx.audio = audio_;
    ctx.dcompDevice = d3d_.DCompDevice();
    ctx.setRootVisual = [this](IDCompositionVisual* v) { return d3d_.SetRootVisual(v); };
    ctx.restoreRootVisual = [this] { d3d_.RestoreRootVisual(); };

    auto candidate = create_backend(request.kind);
    const bool opened = candidate && candidate->Open(request, ctx);

    // 关键：失败的 Open 不得改变呈现方式（否则会把正在工作的画面搞黑）
    const bool selfPresenting = opened && candidate->SelfPresenting();
    const auto decision = arbiter_.OnBackendOpen(selfPresenting, opened);
    if (decision.change) {
        if (decision.target == Presentation::SelfPresenting) d3d_.Suspend();
        else d3d_.Resume();
    }

    if (!opened) {
        wp_log("backend open failed; keeping the previous picture");
        return;
    }

    candidate->SetSpeed(speed_.load());
    candidate->SetPaused(paused_.load());
    lastPaused_ = paused_.load();
    lastSpeed_ = speed_.load();
    wp_log(std::string("backend active: ") + candidate->Name());
    backend_ = std::move(candidate);
}

void FrameSchedulerLoop::thread_main(std::promise<bool> init) {
    // 渲染线程自带 COM 单元；D3D/DComp 与各后端都在此线程创建与销毁
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    bool ok = window_.Create();
    if (ok) {
        ok = d3d_.Create(window_.Handle(), window_.Width(), window_.Height());
    }
    if (!ok) {
        wp_log("FrameSchedulerLoop: window or d3d init failed; wallpaper disabled");
    }

    init.set_value(ok);
    if (!ok) {
        if (SUCCEEDED(comHr)) CoUninitialize();
        return;
    }

    threadId_ = GetCurrentThreadId();
    hwnd_ = window_.Handle();
    running_.store(true);
    wp_log(std::string("FrameSchedulerLoop: running, embedded=") +
           (window_.Embedded() ? "1" : "0"));

    int64_t deadline = qpc_100ns();
    lastMaster_ = ClockMaster::Qpc;

    // 系统默认定时器粒度约 15.6ms，会把 33ms 的等待量化成 31/47ms —— 实测节拍只有
    // 27-29/s，而源是 30fps（丢的这几帧表现为偶发跳帧）。高精度可等待定时器只作用于
    // 本进程，不像 timeBeginPeriod 那样改全局定时器状态。
    HANDLE frameTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                               CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                               TIMER_ALL_ACCESS);
    if (!frameTimer) {
        wp_log("FrameSchedulerLoop: high-resolution timer unavailable; tick timing will jitter");
    }

    // 等一"拍"：有时间上限时交给高精度定时器，否则退回毫秒粒度（向下取整，宁可早醒）
    auto wait_ticks = [&](int64_t hundredNs) {
        if (frameTimer && hundredNs > 0) {
            LARGE_INTEGER due;
            due.QuadPart = -hundredNs;   // 负数 = 相对当前时间
            if (SetWaitableTimer(frameTimer, &due, 0, nullptr, nullptr, FALSE)) {
                MsgWaitForMultipleObjects(1, &frameTimer, FALSE, INFINITE, QS_ALLINPUT);
                return;
            }
        }
        const DWORD ms = hundredNs > 0 ? static_cast<DWORD>(hundredNs / 10000) : 0;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, ms, QS_ALLINPUT);
    };

    // 帧率诊断：每周期的"节拍数 / 内容不同的帧数 / 解码与上屏耗时"。
    // 之前几处卡顿（时钟纪元混用、余量被丢、整帧拷贝）全是靠这行数字定位的，
    // 保留成常驻的低频日志（每 10 秒一行），以便回归时不用重编就能看出来。
    int64_t diagDeadline = qpc_100ns() + 100000000LL;   // 100ns 单位
    int64_t diagStart = qpc_100ns();
    int diagTicks = 0;
    int diagDistinct = 0;
    int64_t diagDecodeUs = 0;
    int64_t diagPresentUs = 0;
    uint32_t diagLastSerial = 0;
    int diagLastW = 0;
    int diagLastH = 0;

    while (!quit_.load()) {
        const int64_t diagNow = qpc_100ns();
        if (diagNow >= diagDeadline) {
            // 用实测经过时间做分母：写成固定 10.0 会把窗口超时误差算成速率偏差
            const double secs = static_cast<double>(diagNow - diagStart) / 10000000.0;
            const std::string backendName = backend_ ? backend_->Name() : "none";
            wp_log("fps diag: frames=" + std::to_string(diagDistinct / secs) + "/s ticks=" +
                   std::to_string(static_cast<int>(diagTicks / secs)) + "/s decode=" +
                   std::to_string(diagTicks > 0 ? diagDecodeUs / diagTicks : 0) + "us present=" +
                   std::to_string(diagTicks > 0 ? diagPresentUs / diagTicks : 0) + "us size=" +
                   std::to_string(diagLastW) + "x" + std::to_string(diagLastH) + " backend=" +
                   backendName);
            diagDeadline = diagNow + 100000000LL;
            diagStart = diagNow;
            diagTicks = diagDistinct = 0;
            diagDecodeUs = diagPresentUs = 0;
        }
        bool quitRequested = false;
        pump_messages(quitRequested);
        if (quitRequested) quit_.store(true);
        if (quit_.load()) break;

        apply_pending_backend();

        // 把暂停/速度的变化转发给后端（后端只在渲染线程上被调用）
        if (backend_) {
            const bool p = paused_.load();
            if (p != lastPaused_) {
                backend_->SetPaused(p);
                lastPaused_ = p;
            }
            const double s = speed_.load();
            if (s != lastSpeed_) {
                backend_->SetSpeed(s);
                lastSpeed_ = s;
            }
        }

        if (paused_.load() || !backend_) {
            // 自呈现型接管时不能去动 DComp，否则会把它盖住
            if (!d3d_.Suspended()) d3d_.Clear(0.05f, 0.05f, 0.06f);
            wait_ticks(10000000);   // 100ms
            deadline = qpc_100ns();
            lastMaster_ = ClockMaster::Qpc;
            continue;
        }

        if (backend_->SelfPresenting()) {
            backend_->Tick();
            ++diagTicks;   // 自呈现型也要计入节拍数，否则日志显示 0/s 像卡死了
            wait_ticks(1600000);    // 约 60Hz
            continue;
        }

        int w = 0, h = 0;
        const int64_t t0 = qpc_100ns();
        if (backend_->ProduceFrame(frameBuffer_, w, h) && w > 0 && h > 0) {
            const int64_t t1 = qpc_100ns();
            diagDecodeUs += (t1 - t0) / 10;
            diagLastW = w;
            diagLastH = h;
            if (!d3d_.PresentBgra(frameBuffer_.data(), w, h, w * 4)) {
                wp_log(std::string("present failed: ") + d3d_.LastError());
            }
            diagPresentUs += (qpc_100ns() - t1) / 10;
        }
        ++diagTicks;
        {
            const uint32_t serial = backend_->FrameSerial();
            if (serial != diagLastSerial) {
                // 后端被换掉时序号会归零，此时只记 1 帧，不能按无符号相减算
                diagDistinct += (serial > diagLastSerial)
                    ? static_cast<int>(serial - diagLastSerial) : 1;
                diagLastSerial = serial;
            }
        }

        // 主时钟：有音轨且未静音时跟音频时钟走（音画不漂），否则用 QPC。
        // 注意：`should_drop_to_catch_up` 那套"落后即丢帧"尚未接线 —— 它需要帧 PTS
        // 而 IVideoSource 目前不暴露 PTS；目前的音画同步靠"用音频时钟做节拍源"达成。
        ClockInputs inputs;
        inputs.hasAudioSource = (audio_ != nullptr);
        inputs.audioMuted = audio_ ? audio_->Muted() : true;
        inputs.audioClockValid = true;   // 由 pick_time_source 按 audioUs 复核

        // 取值与主时钟必须同源（见 ClockPolicy.h 的 TimeSource 注释：曾因此卡到约 10fps）
        const int64_t audioUs = audio_ ? audio_->ClockUs() : -1;
        const TimeSource src = pick_time_source(inputs, qpc_100ns(), audioUs);
        const ClockMaster master = src.master;
        const int64_t now = src.now100ns;
        if (master != lastMaster_) {
            // 两个时钟纪元不同，切换时必须重置期限，否则会瞬间"补上"巨量积压
            deadline = now;
            lastMaster_ = master;
        }

        const double fps = backend_->TargetFps();
        const int64_t frameDuration =
            static_cast<int64_t>(10000000.0 / (fps > 1.0 ? fps : 30.0));
        deadline = next_deadline_not_before(deadline, frameDuration, now);

        const auto sched = schedule_frame(now, deadline);
        // 单次等待上限 100ms，保证停止请求能及时响应
        const int64_t capped = sched.waitHundredNs > 10000000 ? 10000000 : sched.waitHundredNs;
        wait_ticks(capped > 0 ? capped : 0);
    }

    running_.store(false);
    if (frameTimer) CloseHandle(frameTimer);
    if (backend_) {
        backend_->Close();
        backend_.reset();
    }
    // 先撤 D3D/DComp 再销毁窗口，顺序反了会留下游离的合成目标
    d3d_.Destroy();
    window_.Destroy();
    hwnd_ = nullptr;

    if (SUCCEEDED(comHr)) CoUninitialize();
}

} // namespace desktopsticker::wallpaper
