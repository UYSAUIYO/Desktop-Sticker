#pragma once
#include <atomic>
#include <functional>
#include <thread>

#include "desktopsticker/Export.h"

namespace desktopsticker {

class DESKTOPSTICKER_API HotkeyService {
public:
    using Clock = std::function<long long()>; // 毫秒
    using TextInputPredicate = std::function<bool()>;

    explicit HotkeyService(Clock clock = DefaultClock);

    bool Start();
    void Stop();
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_.load(); }

    // 可测试的核心判定：返回 true 表示检测到双击空格
    bool HandleKeyEvent(bool isKeyDown, long long nowMs);

    void SetTextInputPredicate(TextInputPredicate pred) { textInputPredicate_ = std::move(pred); }
    void SetDoublePressWindowMs(long long ms) { windowMs_ = ms; }
    void SetOnDoublePress(std::function<void()> cb) { onDoublePress_ = std::move(cb); }

    static long long DefaultClock();

private:
    void ThreadMain();

    Clock clock_;
    TextInputPredicate textInputPredicate_;
    std::function<void()> onDoublePress_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> running_{false};
    std::thread thread_;
    HHOOK hook_ = nullptr;

    // 双击判定状态（仅钩子线程访问）
    bool keyDown_ = false;
    bool firstPressSeen_ = false;
    long long lastReleaseMs_ = 0;
    long long windowMs_ = 250;
};

} // namespace desktopsticker
