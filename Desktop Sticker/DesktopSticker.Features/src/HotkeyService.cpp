#include "pch.h"
#include "desktopsticker/HotkeyService.h"

#include "desktopsticker/Log.h"

namespace desktopsticker {

namespace {
HotkeyService* g_instance = nullptr;
const UINT kReconfigureMsg = WM_APP + 7;

bool IsTextKey(UINT vk) {
    // 字母/数字/小键盘数字/常用标点，视为“正在输入文字”；修饰键与功能键不算
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return true;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return true;
    if ((vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xE2)) return true; // OEM 标点
    return false;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_instance && g_instance->IsEnabled() &&
        g_instance->IsDoubleSpaceMode()) { // 自定义热键模式下不监听双击空格
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if (info->vkCode == VK_SPACE) {
            g_instance->HandleKeyEvent(down, HotkeyService::DefaultClock());
        } else if (down && IsTextKey(info->vkCode)) {
            // 记录“正在打字”：双击空格只在近期确实输入过文字时才被抑制
            g_instance->NotifyOtherKeyDown();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

HotkeyService::HotkeyService(Clock clock)
    : clock_(std::move(clock)) {}

long long HotkeyService::DefaultClock() {
    return static_cast<long long>(GetTickCount64());
}

bool HotkeyService::HandleKeyEvent(bool isKeyDown, long long nowMs) {
    if (!isKeyDown) {
        keyDown_ = false;
        lastReleaseMs_ = nowMs;
        return false;
    }

    if (keyDown_) {
        // 丢失 key-up 保护：超过 2 秒仍“按住”，说明抬起事件被其他钩子吞掉，强制复位
        if (nowMs - lastSpaceDownMs_ > 2000) {
            keyDown_ = false;
            dstklog::Write(L"hotkey", L"[space] stuck keyDown -> reset");
        } else {
            return false; // 长按重复
        }
    }
    lastSpaceDownMs_ = nowMs;
    keyDown_ = true;

    // 打字保护：最近 1.2 秒内确实在输入文字时不触发（不再按“是否有光标”判断，
    // 那会在桌面/很多窗口上误判，导致双击空格触发不了）
    const bool typingRecently = lastTextKeyMs_.load() != 0 &&
                                (nowMs - lastTextKeyMs_.load()) <= 1200;
    if (typingRecently || (textInputPredicate_ && textInputPredicate_())) {
        firstPressSeen_ = false;
        lastReleaseMs_ = 0;
        dstklog::Write(L"hotkey", L"[space] suppress (typing recently)");
        return false;
    }

    if (!firstPressSeen_) {
        firstPressSeen_ = true;
        lastReleaseMs_ = 0;
        dstklog::Write(L"hotkey", L"[space] armed");
        return false;
    }

    // 第二次按下：检查距上次释放是否在窗口内
    if (lastReleaseMs_ != 0 && (nowMs - lastReleaseMs_) <= windowMs_) {
        firstPressSeen_ = false;
        lastReleaseMs_ = 0;
        dstklog::Write(L"hotkey", L"[space] TRIGGER");
        if (onDoublePress_) onDoublePress_();
        return true;
    }

    firstPressSeen_ = true;
    lastReleaseMs_ = 0;
    dstklog::Write(L"hotkey", L"[space] re-arm (too slow)");
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
