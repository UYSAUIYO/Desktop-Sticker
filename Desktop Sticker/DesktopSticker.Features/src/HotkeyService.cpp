#include "pch.h"
#include "desktopsticker/HotkeyService.h"

namespace desktopsticker {

namespace {
HotkeyService* g_instance = nullptr;

bool IsTextInputForeground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(threadId, &gti)) return false;
    return gti.hwndCaret != nullptr; // 有插入符 → 正在文本输入
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_instance) {
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (info->vkCode == VK_SPACE) {
            const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            g_instance->HandleKeyEvent(down, HotkeyService::DefaultClock());
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

HotkeyService::HotkeyService(Clock clock)
    : clock_(std::move(clock)), textInputPredicate_(IsTextInputForeground) {}

long long HotkeyService::DefaultClock() {
    return static_cast<long long>(GetTickCount64());
}

bool HotkeyService::HandleKeyEvent(bool isKeyDown, long long nowMs) {
    if (!isKeyDown) {
        keyDown_ = false;
        lastReleaseMs_ = nowMs;
        return false;
    }

    if (keyDown_) return false; // 忽略长按重复

    keyDown_ = true;
    if (textInputPredicate_ && textInputPredicate_()) {
        firstPressSeen_ = false;
        lastReleaseMs_ = 0;
        return false;
    }

    if (!firstPressSeen_) {
        firstPressSeen_ = true;
        lastReleaseMs_ = 0;
        return false;
    }

    // 第二次按下：检查距上次释放是否在窗口内
    if (lastReleaseMs_ != 0 && (nowMs - lastReleaseMs_) <= windowMs_) {
        firstPressSeen_ = false;
        lastReleaseMs_ = 0;
        if (onDoublePress_) onDoublePress_();
        return true;
    }

    firstPressSeen_ = true;
    lastReleaseMs_ = 0;
    return false;
}

bool HotkeyService::Start() {
    if (running_.exchange(true)) return false;
    thread_ = std::thread([this]() { ThreadMain(); });
    return true;
}

void HotkeyService::Stop() {
    if (!running_.exchange(false)) return;
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    PostThreadMessageW(GetThreadId(thread_.native_handle()), WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    g_instance = nullptr;
}

void HotkeyService::SetEnabled(bool enabled) {
    enabled_.store(enabled);
}

void HotkeyService::ThreadMain() {
    g_instance = this;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                              GetModuleHandleW(L"DesktopSticker.Features.dll"), 0);
    MSG msg;
    while (running_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
}

} // namespace desktopsticker
