#include "pch.h"
#include "desktopsticker/HotkeyService.h"

namespace desktopsticker {

namespace {
HotkeyService* g_instance = nullptr;
const UINT kReconfigureMsg = WM_APP + 7;

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
    if (nCode == HC_ACTION && g_instance && g_instance->IsEnabled() &&
        g_instance->IsDoubleSpaceMode()) { // 自定义热键模式下不监听双击空格
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

void HotkeyService::SetHotkeyMode(const std::wstring& mode, const std::wstring& customHotkey) {
    {
        std::lock_guard<std::mutex> lock(modeMutex_);
        pendingMode_ = mode.empty() ? L"double-space" : mode;
        pendingCustom_ = customHotkey;
        doubleSpaceMode_.store(pendingMode_ != L"custom");
    }
    if (running_.load() && threadId_ != 0) {
        PostThreadMessageW(threadId_, kReconfigureMsg, 0, 0); // 让 hook 线程重注册热键
    }
}

void HotkeyService::ApplyModeOnHookThread() {
    std::wstring mode, custom;
    {
        std::lock_guard<std::mutex> lock(modeMutex_);
        mode = pendingMode_;
        custom = pendingCustom_;
    }
    if (customRegistered_) {
        UnregisterHotKey(nullptr, 1);
        customRegistered_ = false;
    }
    doubleSpaceMode_.store(mode != L"custom");
    if (!doubleSpaceMode_.load()) {
        UINT modifiers = 0;
        UINT vk = 0;
        if (ParseCustomHotkey(custom, modifiers, vk) &&
            RegisterHotKey(nullptr, 1, modifiers | MOD_NOREPEAT, vk)) {
            customRegistered_ = true; // WM_HOTKEY 由本线程消息循环接收
        } else {
            doubleSpaceMode_.store(true); // 解析/注册失败回退双击空格
        }
    }
}

bool HotkeyService::ParseCustomHotkey(const std::wstring& text, UINT& modifiers, UINT& vk) {
    modifiers = 0;
    vk = 0;
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < text.size() || tokens.empty()) {
        const size_t plus = text.find(L'+', start);
        if (plus == std::wstring::npos) {
            tokens.push_back(text.substr(start));
            break;
        }
        tokens.push_back(text.substr(start, plus - start));
        start = plus + 1;
    }
    if (tokens.empty()) return false;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::wstring& t = tokens[i];
        if (_wcsicmp(t.c_str(), L"alt") == 0) modifiers |= MOD_ALT;
        else if (_wcsicmp(t.c_str(), L"ctrl") == 0 || _wcsicmp(t.c_str(), L"control") == 0) modifiers |= MOD_CONTROL;
        else if (_wcsicmp(t.c_str(), L"shift") == 0) modifiers |= MOD_SHIFT;
        else if (_wcsicmp(t.c_str(), L"win") == 0) modifiers |= MOD_WIN;
        else return false;
    }
    const std::wstring key = tokens.back();
    if (_wcsicmp(key.c_str(), L"space") == 0) vk = VK_SPACE;
    else if (key.size() == 1 && key[0] >= L'A' && key[0] <= L'Z') vk = static_cast<UINT>(key[0]);
    else if (key.size() == 1 && key[0] >= L'a' && key[0] <= L'z') vk = static_cast<UINT>(towupper(key[0]));
    else if (key.size() == 1 && key[0] >= L'0' && key[0] <= L'9') vk = static_cast<UINT>(key[0]);
    else if (!key.empty() && towupper(key[0]) == L'F' && key.size() >= 2) {
        const int n = _wtoi(key.c_str() + 1);
        if (n >= 1 && n <= 24) vk = VK_F1 + static_cast<UINT>(n - 1);
    }
    return vk != 0;
}

void HotkeyService::ThreadMain() {
    g_instance = this;
    threadId_ = GetCurrentThreadId();
    ApplyModeOnHookThread();
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                              GetModuleHandleW(L"DesktopSticker.Features.dll"), 0);
    MSG msg;
    while (running_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kReconfigureMsg) {
            ApplyModeOnHookThread();
            continue;
        }
        if (msg.message == WM_HOTKEY && msg.wParam == 1 && onDoublePress_) {
            onDoublePress_();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (customRegistered_) {
        UnregisterHotKey(nullptr, 1);
        customRegistered_ = false;
    }
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
}

} // namespace desktopsticker
