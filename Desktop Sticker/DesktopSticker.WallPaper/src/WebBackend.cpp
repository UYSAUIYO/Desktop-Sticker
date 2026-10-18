#include "pch.h"
#include "WebBackend.h"

#include "Log.h"
#include "Utf8.h"

#include <filesystem>
#include <shlobj.h>
#include <tlhelp32.h>

using Microsoft::WRL::Callback;

namespace desktopsticker::wallpaper {

namespace {

constexpr wchar_t kVirtualHost[] = L"wallpaper.local";
constexpr DWORD kAsyncTimeoutMs = 15000;

// %LOCALAPPDATA%\DesktopSticker\WallPaperWebView
// 与 ResMon 的 WebView2 数据目录分开：两边的生命周期互不相干，混用会互相牵连
std::wstring web_user_data_dir() {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir = (std::filesystem::path(base) / L"DesktopSticker" / L"WallPaperWebView").wstring();
    }
    if (base) CoTaskMemFree(base);
    return dir;
}

// WebView2 的异步完成回调靠调用线程的消息循环派发：等待时必须继续泵消息，
// 直接 Sleep/等事件会死锁。遇到 WM_QUIT 立刻放弃 —— 不能把退出请求吃掉。
bool pump_until(const std::function<bool()>& done, DWORD timeoutMs) {
    const DWORD start = GetTickCount();
    while (!done()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount() - start > timeoutMs) return false;
        Sleep(5);
    }
    return true;
}

// WebView2 持有完成回调直到异步操作结束；回调可能晚于本函数返回（超时/WM_QUIT/
// 中途 Close）才触发。等待状态必须放堆上由回调自己持有 shared_ptr——
// 按引用捕获本函数的栈变量就是悬垂写。
struct WebView2Await {
    bool done = false;
    bool ok = false;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env;
    Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> composition;
};

// WebView2 只在"本进程拉起来的"浏览器里找宿主窗口：机器上可能还有别的 WebView2（资源监视器、
// 其它应用），按进程名匹配会挪错窗口。
bool is_own_webview2_process(DWORD pid) {
    if (!pid) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    const DWORD self = GetCurrentProcessId();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool ours = false;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                ours = entry.th32ParentProcessID == self;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return ours;
}

struct HostSearch {
    RECT parentRect{};
    HWND found = nullptr;
};

BOOL CALLBACK enum_composition_host(HWND top, LPARAM param) {
    auto* search = reinterpret_cast<HostSearch*>(param);
    if (search->found) return FALSE;

    wchar_t cls[64]{};
    if (GetClassNameW(top, cls, static_cast<int>(std::size(cls))) == 0) return TRUE;
    if (wcscmp(cls, L"Chrome_WidgetWin_1") != 0) return TRUE;

    RECT rc{};
    if (!GetWindowRect(top, &rc)) return TRUE;
    // WebView2 把这个窗口摆在父窗口的位置上，所以位置是它最可靠的指纹
    if (rc.left != search->parentRect.left || rc.top != search->parentRect.top) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(top, &pid);
    if (!is_own_webview2_process(pid)) return TRUE;

    search->found = top;
    return FALSE;
}

} // namespace

bool WebBackend::Open(const BackendRequest& request, const BackendContext& ctx) {
    Close();

    std::error_code ec;
    if (!std::filesystem::is_directory(request.sourcePath, ec)) {
        lastError_ = "web dir missing";
        wp_log("web backend: web dir missing: " + to_utf8(request.sourcePath));
        return false;
    }
    if (!ctx.window || !ctx.dcompDevice || !ctx.setRootVisual) {
        lastError_ = "no composition host";
        wp_log("web backend: no DComp host available for visual hosting");
        return false;
    }

    hwnd_ = ctx.window;
    width_ = ctx.width;
    height_ = ctx.height;
    audio_ = ctx.audio;
    dcompDevice_ = ctx.dcompDevice;
    restoreRoot_ = ctx.restoreRootVisual;

    const std::wstring userData = web_user_data_dir();
    if (userData.empty()) {
        lastError_ = "no user data dir";
        return false;
    }
    std::filesystem::create_directories(userData, ec);

    auto awaitEnv = std::make_shared<WebView2Await>();
    auto envHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [awaitEnv](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (SUCCEEDED(hr) && env) {
                awaitEnv->env = env;
                awaitEnv->ok = true;
            }
            awaitEnv->done = true;
            return S_OK;
        });

    // 运行时缺失（没装 Edge WebView2 Runtime）会在这里失败 → 该后端不可用，其它后端不受影响
    if (FAILED(CreateCoreWebView2EnvironmentWithOptions(nullptr, userData.c_str(), nullptr,
                                                        envHandler.Get()))) {
        lastError_ = "WebView2 environment";
        wp_log("web backend: CreateCoreWebView2EnvironmentWithOptions failed");
        return false;
    }
    if (!pump_until([&] { return awaitEnv->done; }, kAsyncTimeoutMs) || !awaitEnv->ok) {
        lastError_ = "WebView2 environment";
        wp_log("web backend: WebView2 environment unavailable (runtime not installed?)");
        Close();
        return false;
    }
    env_ = awaitEnv->env;

    // CreateCoreWebView2CompositionController 是 ICoreWebView2Environment3 起的接口
    Microsoft::WRL::ComPtr<ICoreWebView2Environment3> env3;
    if (FAILED(env_.As(&env3)) || !env3) {
        lastError_ = "WebView2 runtime too old";
        wp_log("web backend: runtime too old for visual hosting");
        Close();
        return false;
    }

    auto awaitCtl = std::make_shared<WebView2Await>();
    auto ctlHandler = Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
        [awaitCtl](HRESULT hr, ICoreWebView2CompositionController* controller) -> HRESULT {
            if (SUCCEEDED(hr) && controller) {
                awaitCtl->composition = controller;
                awaitCtl->ok = true;
            }
            awaitCtl->done = true;
            return S_OK;
        });
    if (FAILED(env3->CreateCoreWebView2CompositionController(hwnd_, ctlHandler.Get()))) {
        lastError_ = "composition controller";
        wp_log("web backend: CreateCoreWebView2CompositionController failed");
        Close();
        return false;
    }
    if (!pump_until([&] { return awaitCtl->done; }, kAsyncTimeoutMs) || !awaitCtl->ok) {
        lastError_ = "composition controller";
        wp_log("web backend: composition controller unavailable");
        Close();
        return false;
    }
    composition_ = awaitCtl->composition;

    // 视觉宿主对象同时实现 ICoreWebView2Controller（bounds/visible/CoreWebView2 都在那边）
    if (FAILED(composition_.As(&controller_)) || !controller_ ||
        FAILED(controller_->get_CoreWebView2(&core_)) || !core_) {
        lastError_ = "WebView2 core";
        wp_log("web backend: cannot reach ICoreWebView2 from the composition controller");
        Close();
        return false;
    }

    // 壁纸页面不需要跟宿主通信，桥一律关掉；顺手关掉一切浏览器特征
    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(core_->get_Settings(&settings)) && settings) {
        settings->put_IsWebMessageEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        Microsoft::WRL::ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3)) && settings3) {
            settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        }
    }

    // SetVirtualHostNameToFolderMapping 是 ICoreWebView2_3 起才有的：必须 QueryInterface。
    // 用虚拟主机而非 file://：避免本地文件访问限制，页面里的相对路径 JS/CSS 才能加载。
    Microsoft::WRL::ComPtr<ICoreWebView2_3> core3;
    if (FAILED(core_.As(&core3)) || !core3) {
        lastError_ = "WebView2 runtime too old";
        Close();
        return false;
    }
    if (FAILED(core3->SetVirtualHostNameToFolderMapping(
            kVirtualHost, request.sourcePath.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) {
        lastError_ = "virtual host mapping";
        wp_log("web backend: SetVirtualHostNameToFolderMapping failed");
        Close();
        return false;
    }

    // 关键一步：建我们的视觉 → 让 WebView2 把页面挂进它 → 把该视觉设为 DComp 根。
    // 顺序不能反：先挂根再设 target 的话，WebView2 会往一个未合成的视觉上画。
    if (FAILED(dcompDevice_->CreateVisual(&visual_)) || !visual_) {
        lastError_ = "CreateVisual";
        wp_log("web backend: IDCompositionDevice::CreateVisual failed");
        Close();
        return false;
    }
    visual_->SetOffsetX(0.0f);
    visual_->SetOffsetY(0.0f);
    if (FAILED(composition_->put_RootVisualTarget(visual_.Get()))) {
        lastError_ = "RootVisualTarget";
        wp_log("web backend: put_RootVisualTarget failed");
        Close();
        return false;
    }
    if (!ctx.setRootVisual(visual_.Get())) {
        lastError_ = "SetRoot";
        wp_log("web backend: could not make the web visual the DComp root");
        Close();
        return false;
    }
    dcompDevice_->Commit();

    apply_bounds();
    apply_audio();

    // 视觉宿主拿不到系统输入：把自己登记成鼠标接收方（钩子由 WallPaper 模块统一持有）
    relay_ = ctx.mouse;
    if (relay_) relay_->Attach(ctx.window, this);

    const std::wstring url = std::wstring(L"https://") + kVirtualHost + L"/index.html";
    if (FAILED(core_->Navigate(url.c_str()))) {
        lastError_ = "navigate";
        wp_log("web backend: Navigate failed");
        Close();
        return false;
    }

    wp_log("web backend opened (visual hosting): " + to_utf8(request.sourcePath));
    return true;
}

void WebBackend::Close() {
    // 先撤钩子登记，再拆 WebView2：Detach 之后不会再有事件进来
    if (relay_) {
        relay_->Detach(this);
        relay_ = nullptr;
    }
    // 顺序要紧：先把 DComp 根换回我们自己的视觉，再放掉 WebView2 与它的视觉，
    // 否则根视觉会短暂指向一个已释放的对象
    if (visual_ && restoreRoot_) {
        restoreRoot_();
        if (dcompDevice_) dcompDevice_->Commit();
    }
    visual_.Reset();
    if (composition_) {
        composition_->put_RootVisualTarget(nullptr);
        composition_.Reset();
    }
    core_.Reset();
    controller_.Reset();
    env_.Reset();
    hwnd_ = nullptr;
    width_ = height_ = 0;
    muted_ = true;
    paused_ = false;
}

void WebBackend::apply_bounds() {
    if (!controller_) return;

    // 分辨率变化时跟着窗口走（壁纸窗口本身不会重建）
    RECT rc{};
    if (hwnd_ && GetClientRect(hwnd_, &rc)) {
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        if (w > 0 && h > 0) {
            width_ = w;
            height_ = h;
        }
    }

    const RECT bounds{ 0, 0, width_, height_ };
    controller_->put_Bounds(bounds);
    controller_->put_IsVisible(TRUE);

    ensure_host_parked();
}

// WebView2 的视觉宿主（Chrome_WidgetWin_1）是**顶层**窗口：它贴在父窗口的屏幕位置上，
// 全透明（分层属性的 alpha = 0）却照样参与命中测试。实测此时 WindowFromPoint 在任何
// 位置都返回它 —— 整个桌面（图标 / 分区卡片 / 时钟）全都点不动，壁纸自身也拿不到
// 转发（因为"命中桌面"的前提不成立）。把它挪到屏幕外即可：页面画面走的是我们 DComp
// 树里的那个视觉，与这个窗口的位置无关（挪走之后画面不变，命中恢复成 SHELLDLL_DefView）。
// 它可能被 WebView2 在导航 / 尺寸变化后摆回屏幕上，所以 Tick 里低频复查。
void WebBackend::ensure_host_parked() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;

    RECT parent{};
    if (!GetWindowRect(hwnd_, &parent)) return;

    if (!hostWindow_ || !IsWindow(hostWindow_)) {
        HostSearch search;
        search.parentRect = parent;
        EnumWindows(enum_composition_host, reinterpret_cast<LPARAM>(&search));
        if (!search.found) return;   // 还没建出来：下次再试
        hostWindow_ = search.found;
    }

    RECT host{};
    if (!GetWindowRect(hostWindow_, &host)) {
        hostWindow_ = nullptr;
        return;
    }
    if (host.left <= -10000) return;   // 已经在屏幕外

    SetWindowPos(hostWindow_, nullptr, -32000, -32000, host.right - host.left, host.bottom - host.top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    if (!hostParked_) {
        hostParked_ = true;
        wp_log("web backend: parked the WebView2 composition host window (it covers the desktop hit-test)");
    }
}

void WebBackend::apply_audio() {
    if (!core_ || !audio_) return;

    // 只有开关：ICoreWebView2 没有"设置页面音量"的接口，而音量滑块要多写一层脚本注入。
    // 规格只要求"页面可出声 + 全局音频开关"，音量对网页类型不适用（设置页据此置灰）。
    const bool muted = audio_->Muted();
    if (muted == muted_) return;

    Microsoft::WRL::ComPtr<ICoreWebView2_8> core8;
    if (SUCCEEDED(core_.As(&core8)) && core8) {
        core8->put_IsMuted(muted ? TRUE : FALSE);
        muted_ = muted;
    }
}

void WebBackend::Tick() {
    apply_audio();

    RECT rc{};
    if (controller_ && hwnd_ && GetClientRect(hwnd_, &rc)) {
        const int w = rc.right - rc.left;
        if (w > 0 && w != width_) apply_bounds();
    }

    // 宿主窗口可能被摆回屏幕上（导航完成、尺寸变化）：约 1 秒复查一次
    if (++hostCheckTicks_ >= 60) {
        hostCheckTicks_ = 0;
        ensure_host_parked();
    }
}

void WebBackend::SetPaused(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    suspend(paused);
}

void WebBackend::suspend(bool want) {
    Microsoft::WRL::ComPtr<ICoreWebView2_3> core3;
    if (!core_ || FAILED(core_.As(&core3)) || !core3) return;

    if (!want) {
        core3->Resume();
        wp_log("web backend: resumed");
        return;
    }

    auto awaitSuspend = std::make_shared<WebView2Await>();
    auto handler = Callback<ICoreWebView2TrySuspendCompletedHandler>(
        [awaitSuspend](HRESULT hr, BOOL succeeded) -> HRESULT {
            awaitSuspend->ok = SUCCEEDED(hr) && succeeded;
            awaitSuspend->done = true;
            return S_OK;
        });
    if (FAILED(core3->TrySuspend(handler.Get()))) return;
    if (!pump_until([&] { return awaitSuspend->done; }, 3000)) return;
    wp_log(awaitSuspend->ok ? "web backend: suspended"
                            : "web backend: suspend refused (page still active)");
}

// ---------------------------------------------------------------------------
// 鼠标交互
// ---------------------------------------------------------------------------
//
// 视觉宿主（composition controller）拿不到系统输入：所有鼠标事件都要宿主自己
// 通过 SendMouseInput 送进来。调用方是 MouseRelay::Drain（渲染线程），也就是
// 创建这个控制器的同一条线程。
void WebBackend::OnRelayMouse(const MouseRelayEvent& event) {
    if (!composition_) return;

    COREWEBVIEW2_MOUSE_EVENT_KIND kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
    switch (event.kind) {
        case RelayEventKind::Move: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE; break;
        case RelayEventKind::Leave: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE; break;
        case RelayEventKind::LeftDown: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN; break;
        case RelayEventKind::LeftUp: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP; break;
        case RelayEventKind::RightDown: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN; break;
        case RelayEventKind::RightUp: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP; break;
        case RelayEventKind::MiddleDown: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN; break;
        case RelayEventKind::MiddleUp: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP; break;
        case RelayEventKind::Wheel: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL; break;
        case RelayEventKind::HorizontalWheel:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL;
            break;
    }

    // virtualKeys 是"事件发生时按住的键"（含按键本身），不是"本事件要按的键"
    COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
    if (event.buttons & kRelayLeft) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
    if (event.buttons & kRelayRight) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON;
    if (event.buttons & kRelayMiddle) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_MIDDLE_BUTTON;
    if (event.shift) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_SHIFT;
    if (event.ctrl) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_CONTROL;

    const bool wheel = event.kind == RelayEventKind::Wheel ||
                       event.kind == RelayEventKind::HorizontalWheel;
    const UINT32 mouseData = wheel ? static_cast<UINT32>(event.wheelDelta) : 0;
    POINT point{ event.x, event.y };

    const HRESULT hr = composition_->SendMouseInput(kind, keys, mouseData, point);
    if (FAILED(hr) && !inputErrorLogged_) {
        inputErrorLogged_ = true;   // 只报一次：每帧失败的日志会把 debug.log 刷满
        wp_log("web backend: SendMouseInput failed (0x" +
               std::to_string(static_cast<unsigned long>(hr)) + ")");
    }
}

} // namespace desktopsticker::wallpaper
