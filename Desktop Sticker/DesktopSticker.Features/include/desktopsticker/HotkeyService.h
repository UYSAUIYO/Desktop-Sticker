#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
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
    bool IsDoubleSpaceMode() const { return doubleSpaceMode_.load(); }

    // 可测试的核心判定：返回 true 表示检测到双击空格
    bool HandleKeyEvent(bool isKeyDown, long long nowMs);

    // 设置热键方案："double-space"（双击空格）或 "custom"（customHotkey 组合键，如 "Alt+Space"）
    void SetHotkeyMode(const std::wstring& mode, const std::wstring& customHotkey);

    // 钩子线程记录任意非空格按键，用于“正在打字”判定
    void NotifyOtherKeyDown() {
        const long long now = DefaultClock();
        lastTextKeyMs_.store(now);
        if (keyDown_ && now - lastSpaceDownMs_ > 500) {
            keyDown_ = false; // 已按下其他键 → 空格必然已抬起（key-up 丢失保护）
        }
    }

    void SetTextInputPredicate(TextInputPredicate pred) { textInputPredicate_ = std::move(pred); }
    void SetDoublePressWindowMs(long long ms) { windowMs_ = ms; }
    void SetOnDoublePress(std::function<void()> cb) { onDoublePress_ = std::move(cb); }

    static long long DefaultClock();

private:
    void ThreadMain();
    void ApplyModeOnHookThread();
    static bool ParseCustomHotkey(const std::wstring& text, UINT& modifiers, UINT& vk);

    Clock clock_;
    TextInputPredicate textInputPredicate_;
    std::function<void()> onDoublePress_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> running_{false};
    std::atomic<bool> doubleSpaceMode_{true};
    std::thread thread_;
    DWORD threadId_ = 0;
    HHOOK hook_ = nullptr;

    // 热键方案（pending 由 SetHotkeyMode 写入，hook 线程读取并注册自定义热键）
    std::mutex modeMutex_;
    std::wstring pendingMode_ = L"double-space";
    std::wstring pendingCustom_ = L"Alt+Space";
    bool customRegistered_ = false;

    // 双击判定状态（仅钩子线程访问）
    bool keyDown_ = false;
    long long lastSpaceDownMs_ = 0; // 丢失 key-up 保护用
    bool firstPressSeen_ = false;
    long long lastReleaseMs_ = 0;
    long long windowMs_ = 400; // 双击判定窗口：释放到再次按下
    std::atomic<long long> lastTextKeyMs_{0}; // 最近一次非空格按键（用于“正在打字”判定）
};

} // namespace desktopsticker
