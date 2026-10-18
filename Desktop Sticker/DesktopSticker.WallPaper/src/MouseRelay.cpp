#include "pch.h"
#include "MouseRelay.h"

#include <commctrl.h>

#include "Log.h"

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

    std::deque<MouseRelayEvent> pending;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.empty()) return;
        pending.swap(queue_);
    }
    // 暂停/关闭/没有接收者：事件丢弃即可（下次交互会重新对齐悬停态）
    if (paused || !enabled_.load() || !sink_) return;

    // 诊断：前 12 条事件写日志。"壁纸点了没反应"时先确认事件到底有没有到渲染线程。
    static int traced = 0;   // 只有渲染线程进这里，不需要同步
    for (const auto& event : pending) {
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

    if (ready_event_) SetEvent(ready_event_);

    if (hook) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        UnhookWindowsHookEx(hook);
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
        if (was_active_ && inside_) {
            MouseRelayEvent leave;
            leave.kind = RelayEventKind::Leave;
            leave.x = lastClientX_;
            leave.y = lastClientY_;
            leave.screenX = lastScreenX_;
            leave.screenY = lastScreenY_;
            push(leave);
            inside_ = false;
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
        if (inside_) {
            MouseRelayEvent leave;
            leave.kind = RelayEventKind::Leave;
            leave.x = lastClientX_;
            leave.y = lastClientY_;
            leave.screenX = lastScreenX_;
            leave.screenY = lastScreenY_;
            push(leave);
            inside_ = false;
        }
        return;
    }

    int cx = 0;
    int cy = 0;
    if (!relay_to_client(vp, pt.x, pt.y, captured, cx, cy)) return;

    // 图标只对"按下"做命中测试（每次跨进程 SendMessage；移动时不做，否则太贵）
    RelayHit hit;
    classify(pt, relay_is_down(kind), hit);

    const int64_t now = static_cast<int64_t>(GetTickCount64());
    if (!policy_.should_forward(hit, kind, now)) {
        if (inside_) {
            MouseRelayEvent leave;
            leave.kind = RelayEventKind::Leave;
            leave.x = cx;
            leave.y = cy;
            leave.screenX = pt.x;
            leave.screenY = pt.y;
            push(leave);
            inside_ = false;
        }
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

    int buttons = 0;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= kRelayLeft;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= kRelayRight;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) buttons |= kRelayMiddle;
    // 钩子先于系统投递，GetAsyncKeyState 还是"上一条事件"的状态，这里按本事件修正
    if (relay_is_down(kind)) buttons |= relay_button_of(kind);
    if (relay_is_up(kind)) buttons &= ~relay_button_of(kind);
    event.buttons = buttons;

    if (kind == RelayEventKind::Move) {
        event.relX = cx - lastClientX_;
        event.relY = cy - lastClientY_;
    }
    lastClientX_ = cx;
    lastClientY_ = cy;
    lastScreenX_ = pt.x;
    lastScreenY_ = pt.y;

    policy_.note_forwarded(kind, now);
    inside_ = true;
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

void MouseRelay::clear_queue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
}

} // namespace desktopsticker::wallpaper
