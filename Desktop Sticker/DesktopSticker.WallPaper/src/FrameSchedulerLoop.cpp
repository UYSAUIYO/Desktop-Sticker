#include "pch.h"
#include "FrameSchedulerLoop.h"

#include "Log.h"
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

void FrameSchedulerLoop::SetProvider(FrameProvider provider) {
    std::lock_guard<std::mutex> lock(providerMutex_);
    provider_ = std::move(provider);
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

void FrameSchedulerLoop::thread_main(std::promise<bool> init) {
    // 渲染线程自带 COM 单元；DComp/D2D 都在此线程创建与销毁
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

        if (paused_.load()) {
            d3d_.Clear(0.05f, 0.05f, 0.06f);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            deadline = qpc_100ns();
            continue;
        }

        FrameProvider providerCopy;
        {
            std::lock_guard<std::mutex> lock(providerMutex_);
            providerCopy = provider_;
        }

        if (!providerCopy) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            continue;
        }

        int w = 0, h = 0;
        if (providerCopy(frameBuffer_, w, h) && w > 0 && h > 0) {
            if (!d3d_.PresentBgra(frameBuffer_.data(), w, h, w * 4)) {
                wp_log(std::string("present failed: ") + d3d_.LastError());
            }
        }

        // 按源帧率推进期限；错过期限时跳积压不追赶
        const double fps = fpsHint_.load();
        const int64_t frameDuration = static_cast<int64_t>(10000000.0 / (fps > 1.0 ? fps : 30.0));
        const int64_t now = qpc_100ns();
        deadline = next_deadline_not_before(deadline, frameDuration, now);

        const auto sched = schedule_frame(now, deadline);
        // 单次等待上限 100ms，保证停止请求能及时响应
        const DWORD waitMs = sched.waitHundredNs > 0
            ? static_cast<DWORD>(std::min<int64_t>(sched.waitHundredNs / 10000, 100))
            : 0;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, waitMs, QS_ALLINPUT);
    }

    running_.store(false);
    // 先撤 D3D/DComp 再销毁窗口，顺序反了会留下游离的合成目标
    d3d_.Destroy();
    window_.Destroy();
    hwnd_ = nullptr;
    provider_ = nullptr;

    if (SUCCEEDED(comHr)) CoUninitialize();
}

} // namespace desktopsticker::wallpaper
