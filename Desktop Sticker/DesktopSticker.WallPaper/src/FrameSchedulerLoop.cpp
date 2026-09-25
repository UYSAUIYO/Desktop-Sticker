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

    while (!quit_.load()) {
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
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            deadline = qpc_100ns();
            lastMaster_ = ClockMaster::Qpc;
            continue;
        }

        if (backend_->SelfPresenting()) {
            backend_->Tick();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
            continue;
        }

        int w = 0, h = 0;
        if (backend_->ProduceFrame(frameBuffer_, w, h) && w > 0 && h > 0) {
            if (!d3d_.PresentBgra(frameBuffer_.data(), w, h, w * 4)) {
                wp_log(std::string("present failed: ") + d3d_.LastError());
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
        const DWORD waitMs = sched.waitHundredNs > 0
            ? static_cast<DWORD>(std::min<int64_t>(sched.waitHundredNs / 10000, 100))
            : 0;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, waitMs, QS_ALLINPUT);
    }

    running_.store(false);
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
