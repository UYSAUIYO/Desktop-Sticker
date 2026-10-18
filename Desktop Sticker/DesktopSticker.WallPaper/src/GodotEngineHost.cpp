// 内嵌 Godot 宿主：窗口/引擎单例 + 鼠标转发 + 键盘转发（焦点移交代为补充）。
#include "pch.h"
#include "GodotEngineHost.h"

#include "Log.h"
#include "Utf8.h"

#include <filesystem>
#include <vector>

namespace desktopsticker::wallpaper {

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kRuntimeDllName[] = L"godot.windows.template_release.x86_64.dll";
constexpr wchar_t kWndClass[] = L"DstkGodotWallpaper";

fs::path exe_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

// 运行库位置：<exeDir>\godot\godot.windows...dll；找不到就扫同目录里第一个 godot*.dll
std::wstring find_runtime_dll() {
    std::error_code ec;
    const fs::path dir = exe_dir() / L"godot";
    const fs::path exact = dir / kRuntimeDllName;
    if (fs::is_regular_file(exact, ec)) return exact.wstring();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::wstring name = entry.path().filename().wstring();
        if (name.rfind(L"godot", 0) == 0 && entry.path().extension() == L".dll") {
            return entry.path().wstring();
        }
    }
    return {};
}

LRESULT CALLBACK host_wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    // 与壁纸窗口同款：点击穿透（HTTRANSPARENT），不做样式/背景处理。
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, msg, w, l);
}

bool register_window_class() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = host_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    registered = RegisterClassExW(&wc) != 0;
    if (!registered) {
        wp_log("godot host: RegisterClassExW failed: " + std::to_string(GetLastError()));
    }
    return registered;
}

// ---------------------------------------------------------------------------
// 键盘焦点移交（与 Wallpaper Engine 同一条路线，但只是补充路径）
//
// 键盘的主路径是低级键盘钩子转发（GodotEmbeddedBackend::OnRelayKey → post_key），
// 不依赖窗口焦点。这里做的是 WE 的第二步：空白桌面被左键按下时，把前台交给桌面、
// 再把焦点交给引擎窗口 —— 短暂 AttachThreadInput 到当前前台线程（跨进程共享输入
// 队列），SetForegroundWindow(桌面) → SetFocus(引擎窗口)，然后立刻解除附着。
//
// 壁纸窗口是 explorer 的 WorkerW 的 WS_CHILD，本机实测 SetFocus 直接返回
// ERROR_INVALID_PARAMETER (87)，所以这里可能拿不到焦点：拿不到就放弃，键盘仍由
// 钩子转发保证；拿到则引擎开始走原生键盘流，handle_key 会检测到"窗口已有焦点"
// 并停止转发，避免一键两份。
//
// 附着只用一次调用那么久：Godot 的泵是 PeekMessage(nullptr)，长时间附着会把
// Explorer 的消息也捞进本线程。
// ---------------------------------------------------------------------------
bool window_class_is(HWND hwnd, const wchar_t* name) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    wchar_t cls[64]{};
    if (GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) == 0) return false;
    return wcscmp(cls, name) == 0;
}

// 桌面窗口树（Progman / WorkerW / SHELLDLL_DefView / SysListView32）都算"桌面前台"。
bool is_desktop_window(HWND hwnd) {
    if (!hwnd) return false;
    if (window_class_is(hwnd, L"Progman") || window_class_is(hwnd, L"WorkerW") ||
        window_class_is(hwnd, L"SHELLDLL_DefView") || window_class_is(hwnd, L"SysListView32")) {
        return true;
    }
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    return root && root != hwnd && (window_class_is(root, L"Progman") || window_class_is(root, L"WorkerW"));
}

// 场景壁纸结束后，Explorer 承载壁纸的 WorkerW 空置会露白：
// 重设一次注册表里的壁纸路径（内容不变）强制重绘。
void restore_desktop_wallpaper() {
    wchar_t path[MAX_PATH]{};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper",
                     RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS &&
        path[0] != L'\0') {
        SystemParametersInfoW(0x14 /* SPI_SETDESKWALLPAPER */, 0, path,
                              0x3 /* SPIF_UPDATEINIFILE | SPIF_SENDCHANGE */);
        wp_log("godot host: desktop wallpaper restored");
    }
}

} // namespace

GodotEngineHost& GodotEngineHost::instance() {
    static GodotEngineHost host;
    return host;
}

bool GodotEngineHost::runtime_available() const {
    return !find_runtime_dll().empty();
}

bool GodotEngineHost::ensure_runtime(std::string& error) {
    if (dll_) return true;

    const std::wstring path = find_runtime_dll();
    if (path.empty()) {
        error = "缺少 Godot 运行库（godot\\" + to_utf8(kRuntimeDllName) + "）";
        return false;
    }
    dll_ = LoadLibraryExW(path.c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll_) {
        error = "加载 Godot 运行库失败（错误码 " + std::to_string(GetLastError()) + "）";
        wp_log("godot host: LoadLibraryExW failed: " + to_utf8(path));
        return false;
    }

    fn_set_present_ = reinterpret_cast<decltype(fn_set_present_)>(GetProcAddress(dll_, "dstk_embed_set_present_hwnd"));
    fn_create_ = reinterpret_cast<decltype(fn_create_)>(GetProcAddress(dll_, "dstk_godot_create"));
    fn_start_ = reinterpret_cast<decltype(fn_start_)>(GetProcAddress(dll_, "dstk_godot_start"));
    fn_iteration_ = reinterpret_cast<decltype(fn_iteration_)>(GetProcAddress(dll_, "dstk_godot_iteration"));
    fn_pause_ = reinterpret_cast<decltype(fn_pause_)>(GetProcAddress(dll_, "dstk_godot_pause"));
    fn_resume_ = reinterpret_cast<decltype(fn_resume_)>(GetProcAddress(dll_, "dstk_godot_resume"));
    fn_destroy_ = reinterpret_cast<decltype(fn_destroy_)>(GetProcAddress(dll_, "dstk_godot_destroy"));
    fn_load_pack_ = reinterpret_cast<decltype(fn_load_pack_)>(GetProcAddress(dll_, "dstk_godot_load_pack"));
    if (!fn_set_present_ || !fn_create_ || !fn_start_ || !fn_iteration_ || !fn_destroy_ || !fn_load_pack_) {
        error = "Godot 运行库缺少嵌入接口（版本不匹配？）";
        wp_log("godot host: runtime exports missing");
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }
    wp_log("godot host: runtime loaded: " + to_utf8(path));
    return true;
}

bool GodotEngineHost::ensure_window(HWND parent, int width, int height, std::string& error) {
    if (window_ && IsWindow(window_)) return true;
    if (!register_window_class()) {
        error = "注册壁纸窗口类失败";
        return false;
    }

    parent_ = parent;
    const DWORD style = parent ? WS_CHILD : WS_POPUP;
    window_ = CreateWindowExW(WS_EX_NOACTIVATE, kWndClass, L"DstkGodotWallpaper", style,
                              0, 0, width, height, parent, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
        error = "创建壁纸窗口失败（错误码 " + std::to_string(GetLastError()) + "）";
        parent_ = nullptr;
        return false;
    }
    wp_log("godot host: window created " + std::to_string(width) + "x" + std::to_string(height) +
           (parent ? " (embedded in WorkerW)" : " (top-level fallback)"));
    return true;
}

bool GodotEngineHost::ensure_instance(const std::wstring& packPath, const std::string& mainScene, std::string& error) {
    if (instance_) {
        if (packPath == currentPack_) return true;
        // 换包：挂载新 PCK 并切主场景（旧包保持挂载，内存会累积，v1 接受）。
        if (!fn_load_pack_(instance_, to_utf8(packPath).c_str(), mainScene.c_str())) {
            error = "切换壁纸包失败（无法挂载 project.pck）";
            wp_log("godot host: dstk_godot_load_pack failed");
            return false;
        }
        currentPack_ = packPath;
        currentScene_ = mainScene;
        wp_log("godot host: pack switched: " + to_utf8(packPath));
        return true;
    }

    // 首次：引擎只能在隐藏窗口上完成初始注册（AccessKit 子类化适配器的硬要求），
    // 所以顺序固定为 建窗口(隐藏) -> 建实例 -> 启动 -> 显示。
    fn_set_present_(window_);

    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const std::string resArg = std::to_string(width_) + "x" + std::to_string(height_);
    std::vector<std::string> args = {
        exePath,
        "--main-pack", to_utf8(packPath),
        "--resolution", resArg,
        "--", "--dstk-embedded",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& a : args) argv.push_back(a.data());

    instance_ = fn_create_(static_cast<int>(argv.size()), argv.data());
    if (!instance_) {
        error = "创建 Godot 实例失败（壁纸包损坏或与运行库版本不符）";
        wp_log("godot host: dstk_godot_create failed");
        destroy_all();
        return false;
    }
    if (!fn_start_(instance_)) {
        error = "启动 Godot 实例失败";
        wp_log("godot host: dstk_godot_start failed");
        destroy_all();
        return false;
    }
    currentPack_ = packPath;
    currentScene_ = mainScene;
    wp_log("godot host: instance started (pack=" + to_utf8(packPath) + ")");
    return true;
}

bool GodotEngineHost::activate(const std::wstring& packPath, const std::string& mainScene,
                               HWND parent, int width, int height, std::string& error) {
    error.clear();
    lastError_.clear();
    if (packPath.empty() || mainScene.empty()) {
        error = "壁纸包参数不完整";
        return false;
    }
    if (width <= 0 || height <= 0) {
        error = "壁纸尺寸无效";
        return false;
    }

    width_ = width;
    height_ = height;

    if (!ensure_runtime(error) || !ensure_window(parent, width, height, error) ||
        !ensure_instance(packPath, mainScene, error)) {
        lastError_ = error;
        return false;
    }

    // 激活：置顶（同层 sibling 里）并显示。壁纸不抢焦点。
    SetWindowPos(window_, HWND_TOP, 0, 0, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    active_ = true;
    notified_paused_ = false;
    if (fn_resume_) fn_resume_(instance_);
    return true;
}

void GodotEngineHost::deactivate() {
    active_ = false;
    if (window_ && IsWindow(window_)) {
        ShowWindow(window_, SW_HIDE);
    }
}

void GodotEngineHost::tick() {
    if (!active_ || !instance_) return;
    // GodotInstance::iteration() 返回 true = 引擎请求退出（与 OS_Windows::run 同语义）。
    if (fn_iteration_(instance_)) {
        wp_log("godot host: engine requested exit; deactivating scene wallpaper");
        deactivate();
    }
}

void GodotEngineHost::notify_paused(bool paused) {
    if (!instance_) return;
    if (paused == notified_paused_) return;
    notified_paused_ = paused;
    if (paused) {
        if (fn_pause_) fn_pause_(instance_);
    } else if (fn_resume_) {
        fn_resume_(instance_);
    }
}

// 鼠标交互：宿主钩子 → 这里 → PostMessage 给引擎窗口。
// 引擎窗口过程被 fork 换成了薄壳（见 dstk_embed_windows.cpp），鼠标消息走
// DisplayServerWindows::WndProc —— 和真实窗口完全同一条路径，滚动条、悬停进出、
// 按下/抬起计数都由引擎自己维护。薄壳在每条消息之后把系统捕获交还，所以这里
// 转发出去的就是引擎收到的唯一一条流（不会和真实鼠标消息重复）。
bool GodotEngineHost::send_mouse(const MouseRelayEvent& event) {
    if (!active_ || !instance_ || !window_ || !IsWindow(window_)) {
        return false;
    }

    POINT client{ event.screenX, event.screenY };
    if (!ScreenToClient(window_, &client)) return false;

    // 诊断（前 16 条）：屏幕 → 客户区的换算结果。坐标对不上、拖动抖动这类问题
    // 先在这里看"发出去的到底是什么坐标"，再怀疑引擎。
    static int traceCount = 0;
    if (traceCount < 16) {
        ++traceCount;
        RECT wr{};
        GetWindowRect(window_, &wr);
        wp_log("godot mouse: kind=" + std::to_string(static_cast<int>(event.kind)) +
               " screen=" + std::to_string(event.screenX) + "," +
               std::to_string(event.screenY) + " client=" + std::to_string(client.x) + "," +
               std::to_string(client.y) + " winrect=" + std::to_string(wr.left) + "," +
               std::to_string(wr.top) + " " + std::to_string(wr.right - wr.left) + "x" +
               std::to_string(wr.bottom - wr.top));
    }

    WPARAM keyState = 0;
    if (event.buttons & kRelayLeft) keyState |= MK_LBUTTON;
    if (event.buttons & kRelayRight) keyState |= MK_RBUTTON;
    if (event.buttons & kRelayMiddle) keyState |= MK_MBUTTON;
    if (event.shift) keyState |= MK_SHIFT;
    if (event.ctrl) keyState |= MK_CONTROL;

    UINT message = WM_MOUSEMOVE;
    WPARAM wparam = keyState;
    LPARAM lparam = MAKELPARAM(client.x, client.y);

    switch (event.kind) {
        case RelayEventKind::Move:
            break;
        case RelayEventKind::Leave:
            // 引擎有原生的"鼠标离开窗口"消息：它会清掉悬停/发出 MOUSE_EXIT
            message = WM_MOUSELEAVE;
            break;
        case RelayEventKind::LeftDown: message = WM_LBUTTONDOWN; break;
        case RelayEventKind::LeftUp: message = WM_LBUTTONUP; break;
        case RelayEventKind::RightDown: message = WM_RBUTTONDOWN; break;
        case RelayEventKind::RightUp: message = WM_RBUTTONUP; break;
        case RelayEventKind::MiddleDown: message = WM_MBUTTONDOWN; break;
        case RelayEventKind::MiddleUp: message = WM_MBUTTONUP; break;
        case RelayEventKind::Wheel:
            // 滚轮消息的 lParam 是**屏幕**坐标（引擎自己 ScreenToClient）
            message = WM_MOUSEWHEEL;
            wparam = MAKEWPARAM(keyState, static_cast<WORD>(event.wheelDelta));
            lparam = MAKELPARAM(event.screenX, event.screenY);
            break;
        case RelayEventKind::HorizontalWheel:
            message = WM_MOUSEHWHEEL;
            wparam = MAKEWPARAM(keyState, static_cast<WORD>(event.wheelDelta));
            lparam = MAKELPARAM(event.screenX, event.screenY);
            break;
        case RelayEventKind::KeyDown:
        case RelayEventKind::KeyUp:
            return false;   // 键盘走 post_key，不进这条鼠标投递路径
    }

    return PostMessageW(window_, message, wparam, lparam) != FALSE;
}

// 键盘（低级键盘钩子转发）：把按键投给引擎窗口。
//
// 和鼠标同一条路：引擎的 fork 薄壳窗口过程（dstk_embed_windows.cpp）把键盘消息交给
// DisplayServerWindows::WndProc，修饰键、扫描码、Alt 组合键都由 Windows 自己解释，
// 引擎据此生成 InputEventKey。**不需要窗口持有系统焦点** —— 这正是绕开
// "挂在 WorkerW 下的窗口拿不到焦点（SetFocus 返回 87）"的办法。
bool GodotEngineHost::post_key(int vk, int scan, bool sys, bool extended, bool down,
                               bool repeat) {
    if (!window_ || !IsWindow(window_) || !active_) return false;
    LPARAM lparam = 1 | (static_cast<LPARAM>(scan & 0xFF) << 16);
    if (extended) lparam |= (1LL << 24);
    if (repeat) lparam |= (1LL << 30);               // 自动重复：引擎据此标记 echo
    if (!down) lparam |= (1LL << 30) | (1LL << 31);  // 前一次键状态 + 抬起标志
    const UINT message = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN)
                              : (sys ? WM_SYSKEYUP : WM_KEYUP);
    return PostMessageW(window_, message, static_cast<WPARAM>(vk), lparam) != FALSE;
}

// 空白桌面被左键按下：把键盘焦点交给引擎窗口。只在渲染线程调用（窗口归它所有）。
void GodotEngineHost::focus_keyboard() {
    if (!window_ || !IsWindow(window_) || !active_) return;
    if (GetFocus() == window_) return;

    const DWORD myThread = GetCurrentThreadId();
    const DWORD windowThread = GetWindowThreadProcessId(window_, nullptr);
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread =
        foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;

    wchar_t winClass[64]{};
    GetClassNameW(window_, winClass, 64);
    wchar_t fgClass[64]{};
    if (foreground) GetClassNameW(foreground, fgClass, 64);

    const HWND focusBefore = GetFocus();
    SetFocus(window_);
    const DWORD firstErr = GetLastError();
    bool ok = GetFocus() == window_;

    bool attachedFg = false;
    bool attachedWin = false;
    DWORD secondErr = 0;
    if (!ok) {
        // SetFocus 只对"调用线程自己的队列"或"已占用输入队列的线程"生效：
        // 窗口若不是本线程创建的，就要和它所属线程的队列附着。
        if (windowThread != myThread) {
            attachedWin = AttachThreadInput(myThread, windowThread, TRUE) != FALSE;
        }
        // 被吞掉的点击本该把前台交给桌面；这里补上，否则焦点给了窗口而前台仍是别的
        // 应用，按键会先被那个应用看到（这是 WE 同款的 SetForegroundWindow 步骤）。
        if (foregroundThread != 0 && foregroundThread != myThread &&
            foregroundThread != windowThread) {
            attachedFg = AttachThreadInput(myThread, foregroundThread, TRUE) != FALSE;
        }
        if (!is_desktop_window(foreground)) {
            HWND desktop = FindWindowW(L"Progman", nullptr);
            if (!desktop) desktop = FindWindowW(L"WorkerW", nullptr);
            if (desktop) {
                SetForegroundWindow(desktop);
            }
        }
        SetFocus(window_);
        secondErr = GetLastError();
        ok = GetFocus() == window_;
        if (attachedWin) AttachThreadInput(myThread, windowThread, FALSE);
        if (attachedFg) AttachThreadInput(myThread, foregroundThread, FALSE);
    }

    wp_log(std::string("godot host: keyboard focus ") + (ok ? "acquired" : "failed") +
           " (attach=" + ((attachedFg || attachedWin) ? "on" : "off") +
           ", my=" + std::to_string(myThread) +
           ", win=" + std::to_string(windowThread) +
           ", cls=" + to_utf8(std::wstring(winClass)) +
           ", vis=" + (IsWindowVisible(window_) ? "1" : "0") +
           ", en=" + (IsWindowEnabled(window_) ? "1" : "0") +
           ", fg=" + std::to_string(reinterpret_cast<uintptr_t>(foreground)) +
           ", fgt=" + std::to_string(foregroundThread) +
           ", fgcls=" + to_utf8(std::wstring(fgClass)) +
           ", focus0=" + std::to_string(reinterpret_cast<uintptr_t>(focusBefore)) +
           ", err1=" + std::to_string(firstErr) +
           ", err2=" + std::to_string(secondErr) + ")");
}

void GodotEngineHost::destroy_all() {
    if (instance_) {
        fn_destroy_(instance_);   // 内部 Main::cleanup，会连带销毁它持有的窗口
        instance_ = nullptr;
    }
    if (window_ && IsWindow(window_)) {
        DestroyWindow(window_);
    } else {
        // 引擎清理时可能已经销毁了窗口；句柄失效仅计数用。
    }
    window_ = nullptr;
}

void GodotEngineHost::shutdown() {
    const bool was_embedded = parent_ != nullptr;
    destroy_all();
    parent_ = nullptr;
    currentPack_.clear();
    currentScene_.clear();
    active_ = false;
    notified_paused_ = false;

    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
    fn_set_present_ = nullptr;
    fn_create_ = nullptr;
    fn_start_ = nullptr;
    fn_iteration_ = nullptr;
    fn_pause_ = nullptr;
    fn_resume_ = nullptr;
    fn_destroy_ = nullptr;
    fn_load_pack_ = nullptr;

    if (was_embedded) {
        restore_desktop_wallpaper();
    }
}

} // namespace desktopsticker::wallpaper
