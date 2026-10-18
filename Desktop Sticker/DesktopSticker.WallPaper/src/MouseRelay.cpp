#include "pch.h"
#include "MouseRelay.h"

#include <commctrl.h>

#include <vector>

#include "Log.h"
#include "Utf8.h"

namespace desktopsticker::wallpaper {

namespace {

// 低级钩子只能拿到一个 C 回调，实例指针放这里（同一进程内同时只有一个壁纸后端）
std::atomic<MouseRelay*> g_relay{ nullptr };

constexpr size_t kMaxQueuedEvents = 64;
constexpr DWORD kIconHitTimeoutMs = 100;

const char* kind_name(RelayEventKind kind) {
    switch (kind) {
        case RelayEventKind::Move: return "move";
        case RelayEventKind::Leave: return "leave";
        case RelayEventKind::LeftDown: return "left-down";
        case RelayEventKind::LeftUp: return "left-up";
        case RelayEventKind::RightDown: return "right-down";
        case RelayEventKind::RightUp: return "right-up";
        case RelayEventKind::MiddleDown: return "middle-down";
        case RelayEventKind::MiddleUp: return "middle-up";
        case RelayEventKind::Wheel: return "wheel";
        case RelayEventKind::HorizontalWheel: return "hwheel";
        case RelayEventKind::KeyDown: return "key-down";
        case RelayEventKind::KeyUp: return "key-up";
    }
    return "?";
}

bool class_is(HWND hwnd, const wchar_t* name) {
    wchar_t cls[64]{};
    if (!hwnd || GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) == 0) return false;
    return wcscmp(cls, name) == 0;
}

BOOL CALLBACK enum_find_def_view(HWND top, LPARAM param) {
    const HWND def_view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!def_view) return TRUE;
    *reinterpret_cast<HWND*>(param) = def_view;
    return FALSE;
}

} // namespace

MouseRelay::~MouseRelay() {
    stop_thread();
}

// ---------------------------------------------------------------------------
// 渲染线程侧
// ---------------------------------------------------------------------------

void MouseRelay::Attach(HWND window, IMouseSink* sink) {
    if (sink_ == sink && window_ == window && thread_.joinable()) return;

    // 同一时刻只服务一个后端：先停旧的钩子线程，再改（钩子线程读这些字段）
    stop_thread();
    sink_ = sink;
    window_ = window;
    clear_queue();
    policy_.reset();
    focus_request_.store(false);
    inside_ = false;
    was_active_ = false;
    lastClientX_ = lastClientY_ = 0;
    lastScreenX_ = lastScreenY_ = 0;
    list_view_ = nullptr;
    start_thread();
}

void MouseRelay::Detach(IMouseSink* sink) {
    if (sink != sink_) return;
    stop_thread();   // 先停线程，再清窗口/接收者（钩子线程会读它们）
    window_ = nullptr;
    sink_ = nullptr;
    clear_queue();
}

void MouseRelay::Drain(bool paused) {
    active_.store(!paused);

    // 诊断：5 秒一行的计数。"拖动抽搐"这类问题看 moves/rejected/leaves 的比例就能判断
    // 是命中在抖（被拒很多）、还是我们在造 Leave（leaves 很多）。
    const DWORD nowMs = GetTickCount();
    if (nowMs - lastStatTick_ >= 5000) {
        lastStatTick_ = nowMs;
        if (statMoves_ || statButtons_ || statWheels_ || statLeaves_ || statRejected_ ||
            statReleases_ || statKeys_) {
            wp_log("mouse relay: 5s moves=" + std::to_string(statMoves_) + " buttons=" +
                   std::to_string(statButtons_) + " wheels=" + std::to_string(statWheels_) +
                   " leaves=" + std::to_string(statLeaves_) + " rejected=" +
                   std::to_string(statRejected_) + " releases=" + std::to_string(statReleases_) +
                   " keys=" + std::to_string(statKeys_));
        }
        statMoves_ = statButtons_ = statWheels_ = statLeaves_ = statRejected_ = statReleases_ =
            statKeys_ = 0;
    }

    std::deque<MouseRelayEvent> pending;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.empty()) return;
        pending.swap(queue_);
    }
    // 暂停/关闭/没有接收者：事件丢弃即可（下次交互会重新对齐悬停态）
    if (paused || !enabled_.load() || !sink_) {
        focus_request_.store(false);
        return;
    }

    // 空白桌面左键按下：先把键盘焦点交给后端窗口，再把事件交出去。
    // 焦点必须在渲染线程上移交（窗口归这条线程所有），所以由钩子置位、这里执行。
    if (focus_request_.exchange(false)) {
        sink_->OnRelayFocusRequest();
    }

    // 诊断：前 12 条事件写日志。"壁纸点了没反应"时先确认事件到底有没有到渲染线程。
    static int traced = 0;   // 只有渲染线程进这里，不需要同步
    std::vector<RelayEventKind> kinds;
    kinds.reserve(pending.size());
    for (const auto& e : pending) kinds.push_back(e.kind);

    for (size_t i = 0; i < pending.size(); ++i) {
        const MouseRelayEvent& event = pending[i];
        // 键盘不参与鼠标的"移动被按钮挤后"的排序规则，直接按到达顺序交出去
        if (event.isKey) {
            sink_->OnRelayKey(event);
            continue;
        }
        // 引擎泵每帧先派发按键/滚轮、最后才派发一条移动，所以"后面还有按钮"的移动实际会
        // 晚于按钮执行，位置更旧 → 按下/抬起那一帧画面往回跳。这类移动直接丢弃
        // （按钮自带最新坐标），顺序语义就与真实窗口一致了。
        if (relay_move_superseded(kinds.data(), kinds.size(), i)) {
            ++statRejected_;
            continue;
        }
        if (traced < 12) {
            ++traced;
            wp_log(std::string("mouse relay: ") + kind_name(event.kind) + " x=" +
                   std::to_string(event.x) + " y=" + std::to_string(event.y) + " buttons=" +
                   std::to_string(event.buttons) + " wheel=" + std::to_string(event.wheelDelta));
        }
        sink_->OnRelayMouse(event);
    }
}

// ---------------------------------------------------------------------------
// 钩子线程
// ---------------------------------------------------------------------------

void MouseRelay::start_thread() {
    if (thread_.joinable()) return;
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this]() {
        SetThreadDescription(GetCurrentThread(), L"壁纸鼠标转发");
        thread_main();
    });
    if (ready_event_) {
        // 等钩子装好再返回：Attach 之后的第一条输入就该被拦到
        WaitForSingleObject(ready_event_, 3000);
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }
}

void MouseRelay::stop_thread() {
    if (!thread_.joinable()) return;
    if (thread_id_) {
        PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    }
    thread_.join();
    thread_id_ = 0;
}

void MouseRelay::thread_main() {
    thread_id_ = GetCurrentThreadId();
    g_relay.store(this);

    HMODULE module = GetModuleHandleW(L"DesktopSticker.WallPaper.dll");
    if (!module) module = GetModuleHandleW(nullptr);
    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, hook_proc, module, 0);
    if (!hook) {
        wp_log("mouse relay: SetWindowsHookExW failed: " + std::to_string(GetLastError()));
    }
    // 键盘与鼠标同一个钩子线程、同一套开关：桌面是前台时把按键投给引擎窗口，
    // 普通按键转发后吞掉（见 handle_key / key_proc）。
    HHOOK key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, key_proc, module, 0);
    if (!key_hook) {
        wp_log("mouse relay: keyboard hook failed: " + std::to_string(GetLastError()));
    }

    if (ready_event_) SetEvent(ready_event_);

    if (hook || key_hook) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (hook) UnhookWindowsHookEx(hook);
        if (key_hook) UnhookWindowsHookEx(key_hook);
    }

    g_relay.store(nullptr);
    thread_id_ = 0;
}

LRESULT CALLBACK MouseRelay::hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        MouseRelay* self = g_relay.load();
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        if (self && info) {
            bool swallow = false;
            self->handle_mouse(wparam, *info, swallow);
            if (swallow) return 1;   // 吞掉：桌面不再同时响应
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK MouseRelay::key_proc(int code, WPARAM wparam, LPARAM lparam) {
    bool swallow = false;
    if (code == HC_ACTION) {
        MouseRelay* self = g_relay.load();
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (self && info) {
            self->handle_key(wparam, *info, swallow);
        }
    }
    // 转发过的普通按键要吞掉：否则桌面/输入法也会拿到同一份 —— 中文输入法会在
    // 壁纸上弹出候选窗，真实按键和 IME 注入的 VK_PROCESSKEY 还会让引擎收到两份。
    // 系统组合键（Win / Ctrl / Alt 组合、PrintScreen）一律放行，见 handle_key。
    if (swallow) return 1;
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

// 桌面是前台吗？失败时返回 false（宁可不转发，也不从别的应用手里抢键）。
bool MouseRelay::desktop_foreground() const {
    const HWND fg = GetForegroundWindow();
    if (!fg) return false;

    // 壁纸窗口自己（极端情况下引擎窗口会成为前台）
    if (window_ && IsWindow(window_) && (fg == window_ || IsChild(window_, fg))) return true;

    wchar_t cls[64]{};
    if (GetClassNameW(fg, cls, static_cast<int>(std::size(cls))) == 0) return false;
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
}

void MouseRelay::handle_key(WPARAM msg, const KBDLLHOOKSTRUCT& info, bool& swallow) {
    swallow = false;
    if (msg != WM_KEYDOWN && msg != WM_KEYUP && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP) {
        return;
    }
    if (!enabled_.load() || !active_.load() || !sink_) return;
    if (!desktop_foreground()) {
        // 诊断（前 8 次）：键盘没转发时先看"谁在前台"——别在记事本里试按键
        if (keyRejectLogs_ < 8) {
            ++keyRejectLogs_;
            const HWND fg = GetForegroundWindow();
            wchar_t cls[64]{};
            if (fg) GetClassNameW(fg, cls, static_cast<int>(std::size(cls)));
            GUITHREADINFO info{};
            info.cbSize = sizeof(info);
            GetGUIThreadInfo(0, &info);
            wp_log(std::string("key relay: fg is not desktop (fg=") +
                   std::to_string(reinterpret_cast<uintptr_t>(fg)) + ", cls=" +
                   to_utf8(std::wstring(cls)) + ", focus=" +
                   std::to_string(reinterpret_cast<uintptr_t>(info.hwndFocus)) + ")");
        }
        return;
    }

    // 引擎窗口已经持有焦点（某些系统上 SetFocus 能成功）：原生键盘流在工作，
    // 再转发一份就会出现"按一下收到两次"。
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (window_ && IsWindow(window_) && GetGUIThreadInfo(0, &gti) && gti.hwndFocus &&
        (gti.hwndFocus == window_ || IsChild(window_, gti.hwndFocus))) {
        if (keyRejectLogs_ < 8) {
            ++keyRejectLogs_;
            wp_log("key relay: engine window already has focus, native keys are used");
        }
        return;
    }

    MouseRelayEvent event;
    event.isKey = true;
    event.kind = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ? RelayEventKind::KeyDown
                                                             : RelayEventKind::KeyUp;
    event.vk = static_cast<int>(info.vkCode);
    event.scan = static_cast<int>(info.scanCode);
    event.extended = (info.flags & LLKHF_EXTENDED) != 0;
    event.sys = (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP);
    event.down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    // 自动重复（按住不放）在低级钩子里和第一次按下长得一样，只能自己记：
    // 引擎的窗口过程按 lParam 的 bit30 标记 echo，这里补上，场景才能区分
    // "按住重复"与"按了几次"。
    const bool repeat = event.down && keys_down_.count(event.vk) != 0;
    if (event.down) {
        keys_down_.insert(event.vk);
    } else {
        keys_down_.erase(event.vk);
    }
    event.repeat = repeat;
    event.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    event.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    event.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    // 放行规则（不吞 = 同时也交给系统）：
    //   · Win 组合键（开始菜单、Win+D 等）、PrintScreen 属于系统；
    //   · 任何 Ctrl / Alt 组合（Alt+Tab、Alt+F4、Ctrl+C……）必须留给前台生效。
    // 其余按键转发后吞掉：桌面没有文本输入（图标列表被藏了），吞掉之后输入法不会
    // 在壁纸上弹候选窗，引擎也只收到这一份。
    const bool with_mod = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const int vk = static_cast<int>(info.vkCode);
    const bool keep_system = vk == VK_LWIN || vk == VK_RWIN || vk == VK_SNAPSHOT;
    swallow = !(with_mod || keep_system);

    ++statKeys_;
    push(event);
}

RelayViewport MouseRelay::viewport() const {
    RelayViewport vp{};
    RECT rc{};
    if (window_ && IsWindow(window_) && GetWindowRect(window_, &rc)) {
        vp.x = rc.left;
        vp.y = rc.top;
        vp.width = rc.right - rc.left;
        vp.height = rc.bottom - rc.top;
    }
    return vp;
}

void MouseRelay::handle_mouse(WPARAM msg, const MSLLHOOKSTRUCT& info, bool& swallow) {
    swallow = false;

    const bool active = enabled_.load() && active_.load();
    if (!active) {
        // 暂停/关闭设置时，钩子仍在（省得反复装卸），但一条都不拦
        if (was_active_) {
            release_all_buttons();
            if (inside_) {
                MouseRelayEvent leave;
                leave.kind = RelayEventKind::Leave;
                leave.x = lastClientX_;
                leave.y = lastClientY_;
                leave.screenX = lastScreenX_;
                leave.screenY = lastScreenY_;
                push(leave);
                ++statLeaves_;
                inside_ = false;
            }
        }
        was_active_ = false;
        policy_.reset();
        return;
    }
    was_active_ = true;

    RelayEventKind kind = RelayEventKind::Move;
    int wheelDelta = 0;
    switch (msg) {
        case WM_MOUSEMOVE:     kind = RelayEventKind::Move; break;
        case WM_LBUTTONDOWN:   kind = RelayEventKind::LeftDown; break;
        case WM_LBUTTONUP:     kind = RelayEventKind::LeftUp; break;
        case WM_RBUTTONDOWN:   kind = RelayEventKind::RightDown; break;
        case WM_RBUTTONUP:     kind = RelayEventKind::RightUp; break;
        case WM_MBUTTONDOWN:   kind = RelayEventKind::MiddleDown; break;
        case WM_MBUTTONUP:     kind = RelayEventKind::MiddleUp; break;
        case WM_MOUSEWHEEL:
            kind = RelayEventKind::Wheel;
            wheelDelta = GET_WHEEL_DELTA_WPARAM(info.mouseData);
            break;
        case WM_MOUSEHWHEEL:
            kind = RelayEventKind::HorizontalWheel;
            wheelDelta = GET_WHEEL_DELTA_WPARAM(info.mouseData);
            break;
        default:
            return;   // XBUTTON / 非客户区事件：不碰
    }

    const POINT pt = info.pt;
    const RelayViewport vp = viewport();
    const bool captured = policy_.capturing();
    const bool inside = relay_viewport_contains(vp, pt.x, pt.y);

    // 拖动中即使光标拖出画面也要继续转发（和真实窗口的鼠标捕获一致）
    if (!inside && !captured) {
        release_all_buttons();   // 离开画面时把"按着"的状态交还给后端，别留一个一直按下的键
        if (inside_) {
            MouseRelayEvent leave;
            leave.kind = RelayEventKind::Leave;
            leave.x = lastClientX_;
            leave.y = lastClientY_;
            leave.screenX = lastScreenX_;
            leave.screenY = lastScreenY_;
            push(leave);
            ++statLeaves_;
            inside_ = false;
        }
        return;
    }

    int cx = 0;
    int cy = 0;
    if (!relay_to_client(vp, pt.x, pt.y, captured, cx, cy)) return;

    // 悬停节流：不够久就直接返回，连 WindowFromPoint 都不做 —— 1000Hz 鼠标下
    // 一次命中测试比一次排队贵两个数量级，钩子里做这么重的事会让光标本身发飘。
    const int64_t now = static_cast<int64_t>(GetTickCount64());
    if (kind == RelayEventKind::Move && !captured && !policy_.move_due(now)) {
        return;
    }

    // 图标只对"按下"做命中测试（每次跨进程 SendMessage；移动时不做，否则太贵）
    RelayHit hit;
    classify(pt, relay_is_down(kind), hit);

    if (!policy_.should_forward(hit, kind, now)) {
        // 命中不归壁纸（图标 / 分区卡片 / 任务栏 / 别的应用）：这一条不转发就够了。
        // **不要**在这里下发 Leave —— 光标还在画面里，只是压在了别的东西上；
        // 一发 Leave 就会让后端在"移动 / 离开"之间抖动（拖动时表现为抽搐）。
        ++statRejected_;
        return;
    }

    MouseRelayEvent event;
    event.kind = kind;
    event.x = cx;
    event.y = cy;
    event.screenX = pt.x;
    event.screenY = pt.y;
    event.wheelDelta = wheelDelta;
    event.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    event.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    event.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    int physical = 0;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) physical |= kRelayLeft;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) physical |= kRelayRight;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) physical |= kRelayMiddle;
    event.buttons = relay_reported_buttons(physical, policy_.forwarded_buttons(), kind);

    if (kind == RelayEventKind::Move) {
        // 重新进入画面后的第一条移动不带相对位移：否则会拿"上一次离开前的位置"做差，
        // 后端会收到一个几百像素的瞬移（用 relative 做视差/拖动的场景直接跳一下）。
        event.relX = inside_ ? (cx - lastClientX_) : 0;
        event.relY = inside_ ? (cy - lastClientY_) : 0;
    }
    lastClientX_ = cx;
    lastClientY_ = cy;
    lastScreenX_ = pt.x;
    lastScreenY_ = pt.y;

    policy_.note_forwarded(kind, now);
    inside_ = true;

    // 空白桌面被左键按下：请求把键盘焦点交给后端窗口（渲染线程执行）。
    // 与 Wallpaper Engine 同一条路线：点击吞掉之后必须主动移交焦点，
    // 否则真实按键仍然落在桌面上，壁纸永远收不到键盘。
    if (relay_should_request_focus(kind, hit)) {
        focus_request_.store(true);
    }

    switch (kind) {
        case RelayEventKind::Move: ++statMoves_; break;
        case RelayEventKind::Wheel:
        case RelayEventKind::HorizontalWheel: ++statWheels_; break;
        case RelayEventKind::Leave: break;
        default: ++statButtons_; break;
    }
    push(event);

    // 按下/抬起/滚轮一律吞掉；移动只在按住按钮时吞（否则桌面的悬停与光标会僵住）
    swallow = relay_swallows(kind, captured || policy_.capturing());
}

void MouseRelay::classify(const POINT& pt, bool needIconCheck, RelayHit& hit) {
    hit = RelayHit{};

    const HWND target = WindowFromPoint(pt);
    if (!target) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    hit.ownSurface = pid == GetCurrentProcessId();

    const HWND root = GetAncestor(target, GA_ROOT);
    hit.desktopLayer = class_is(target, L"SysListView32") || class_is(target, L"SHELLDLL_DefView") ||
                       class_is(root, L"Progman") || class_is(root, L"WorkerW");

    if (needIconCheck && hit.desktopLayer) {
        hit.overIcon = icon_at(pt);
    }
}

HWND MouseRelay::find_desktop_list_view() {
    HWND defView = nullptr;
    EnumWindows(enum_find_def_view, reinterpret_cast<LPARAM>(&defView));
    if (!defView) return nullptr;
    return FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
}

bool MouseRelay::icon_at(const POINT& pt) {
    // 图标列表不在我们进程里：LVM_HITTEST 的结构体必须写在 Explorer 的地址空间
    if (!list_view_ || !IsWindow(list_view_)) {
        list_view_ = find_desktop_list_view();
    }
    if (!list_view_) return false;

    // 图标列表**看不见**时它不占任何东西：桌面贴纸在正常模式下把整个列表 (SW_HIDE) 藏了，
    // 图标由分区卡片展示。此时若仍按 LVM_HITTEST 判"压在图标上"，整个桌面会变成一片
    // 看不见的雷区 —— 光标每经过一个原图标格子就被拒一次，拖动直接抽搐（用户实测症状）。
    if (!IsWindowVisible(list_view_)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(list_view_, &pid);
    if (!pid || pid == GetCurrentProcessId()) return false;

    HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                                 FALSE, pid);
    if (!process) return false;

    bool overIcon = false;
    void* remote = VirtualAllocEx(process, nullptr, sizeof(LVHITTESTINFO),
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote) {
        POINT client = pt;
        ScreenToClient(list_view_, &client);
        LVHITTESTINFO ht{};
        ht.pt = client;
        WriteProcessMemory(process, remote, &ht, sizeof(ht), nullptr);

        DWORD_PTR result = 0;
        // 超时给得小：钩子回调里不能久留，超过系统的 LowLevelHooksTimeout 钩子会被静默摘掉
        if (SendMessageTimeoutW(list_view_, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(remote),
                                SMTO_ABORTIFHUNG, kIconHitTimeoutMs, &result)) {
            ReadProcessMemory(process, remote, &ht, sizeof(ht), nullptr);
            overIcon = ht.iItem >= 0;
        }
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    }
    CloseHandle(process);
    return overIcon;
}

void MouseRelay::push(MouseRelayEvent event) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    // 同一帧里排着好几条移动，后端会在同一帧内反复改位置（画面看着像抖动）；
    // 只保留最新位置，把相对位移累加起来，一次 Drain 最多交一条移动给后端。
    if (event.kind == RelayEventKind::Move && !queue_.empty() &&
        queue_.back().kind == RelayEventKind::Move) {
        MouseRelayEvent& last = queue_.back();
        last.x = event.x;
        last.y = event.y;
        last.screenX = event.screenX;
        last.screenY = event.screenY;
        last.relX += event.relX;
        last.relY += event.relY;
        last.buttons = event.buttons;
        last.ctrl = event.ctrl;
        last.shift = event.shift;
        last.alt = event.alt;
        return;
    }

    if (queue_.size() >= kMaxQueuedEvents) {
        // 满了先丢最老的一条移动事件：位置可以丢，按键/滚轮不能丢
        const auto it = std::find_if(queue_.begin(), queue_.end(),
                                     [](const MouseRelayEvent& e) {
                                         return e.kind == RelayEventKind::Move;
                                     });
        if (it != queue_.end()) {
            queue_.erase(it);
        } else {
            queue_.pop_front();
        }
    }
    queue_.push_back(event);
}

void MouseRelay::release_all_buttons() {
    const int held = policy_.forwarded_buttons();
    if (!held) return;

    const RelayEventKind kinds[3] = { RelayEventKind::LeftUp, RelayEventKind::RightUp,
                                      RelayEventKind::MiddleUp };
    const int bits[3] = { kRelayLeft, kRelayRight, kRelayMiddle };
    for (int i = 0; i < 3; ++i) {
        if (!(held & bits[i])) continue;
        MouseRelayEvent up;
        up.kind = kinds[i];
        up.x = lastClientX_;
        up.y = lastClientY_;
        up.screenX = lastScreenX_;
        up.screenY = lastScreenY_;
        up.buttons = held & ~bits[i];
        push(up);
        ++statReleases_;
    }
    policy_.reset();
}

void MouseRelay::clear_queue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
}

} // namespace desktopsticker::wallpaper
