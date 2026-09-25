#include "pch.h"
#include "WebViewHost.h"

#include "desktopsticker/resmon/JsonBuild.h"

using Microsoft::WRL::Callback;

namespace desktopsticker::resmon {

namespace {

constexpr DWORD kAsyncTimeoutMs = 15000;

// WebView2 的异步完成回调要靠调用线程的消息循环派发：
// 因此等待时必须继续泵消息，否则会死等（直接 Sleep/等事件会死锁）。
bool pump_until(const std::function<bool()>& done, DWORD timeoutMs) {
    const DWORD start = GetTickCount();
    while (!done()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount() - start > timeoutMs) return false;
        Sleep(5);
    }
    return true;
}

} // namespace

WebViewHost::~WebViewHost() {
    Destroy();
}

bool WebViewHost::Create(HWND hwnd, const std::wstring& assetsDir, const std::wstring& userDataDir,
                         CommandHandler handler) {
    Destroy();

    hwnd_ = hwnd;
    assetsDir_ = assetsDir;
    handler_ = std::move(handler);

    RECT rc{};
    if (GetClientRect(hwnd_, &rc)) {
        width_ = rc.right - rc.left;
        height_ = rc.bottom - rc.top;
    }

    if (userDataDir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(userDataDir, ec);

    bool envDone = false;
    bool envOk = false;
    HRESULT envResult = E_FAIL;
    auto envHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [&](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            envResult = hr;
            if (SUCCEEDED(hr) && env) {
                env_ = env;
                envOk = true;
            }
            envDone = true;
            return S_OK;
        });

    const HRESULT startHr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataDir.c_str(), nullptr, envHandler.Get());
    if (FAILED(startHr)) return false;
    if (!pump_until([&] { return envDone; }, kAsyncTimeoutMs) || !envOk) {
        (void)envResult;
        return false;
    }

    bool ctlDone = false;
    bool ctlOk = false;
    auto ctlHandler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [&](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
            if (SUCCEEDED(hr) && controller) {
                controller_ = controller;
                ctlOk = true;
            }
            ctlDone = true;
            return S_OK;
        });
    if (FAILED(env_->CreateCoreWebView2Controller(hwnd_, ctlHandler.Get()))) return false;
    if (!pump_until([&] { return ctlDone; }, kAsyncTimeoutMs) || !ctlOk) return false;

    if (FAILED(controller_->get_CoreWebView2(&core_)) || !core_) return false;

    // put_IsWebMessageEnabled 在 ICoreWebView2Settings 上，不在 ICoreWebView2 上
    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(core_->get_Settings(&settings)) && settings) {
        settings->put_IsWebMessageEnabled(TRUE);
    }

    if (!assetsDir_.empty()) {
        // SetVirtualHostNameToFolderMapping 是 ICoreWebView2_3 起才有的：必须 QueryInterface。
        // 用虚拟主机映射而非 file://：避免本地文件访问限制，且相对路径的 JS/CSS 可正常加载。
        Microsoft::WRL::ComPtr<ICoreWebView2_3> core3;
        if (SUCCEEDED(core_.As(&core3)) && core3) {
            core3->SetVirtualHostNameToFolderMapping(
                L"resmon.local", assetsDir_.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        } else {
            // 运行时过旧也会走到这里：页面会 404，由导航失败分支给错误页
            ready_ = false;
        }
    }

    auto msgHandler = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            return on_web_message(args);
        });
    if (FAILED(core_->add_WebMessageReceived(msgHandler.Get(), &msgToken_))) return false;

    auto navHandler = Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            BOOL ok = FALSE;
            if (args) args->get_IsSuccess(&ok);
            if (!ok && core_) {
                // 前端资源缺失时给内置错误页，不留白屏
                core_->NavigateToString(
                    L"<html><body style='font-family:Segoe UI,sans-serif;padding:28px;"
                    L"background:#1f1f1f;color:#eee'><h2>界面资源加载失败</h2>"
                    L"<p>未找到 resmon\\index.html。请重新构建以部署前端资源。</p>"
                    L"</body></html>");
            }
            return S_OK;
        });
    if (FAILED(core_->add_NavigationCompleted(navHandler.Get(), &navToken_))) return false;

    set_bounds();
    if (FAILED(core_->Navigate(L"https://resmon.local/index.html"))) return false;

    ready_ = true;
    return true;
}

void WebViewHost::set_bounds() {
    if (!controller_) return;
    const RECT bounds{ 0, 0, width_, height_ };
    controller_->put_Bounds(bounds);
    controller_->put_IsVisible(TRUE);
}

void WebViewHost::Resize(int width, int height) {
    width_ = width;
    height_ = height;
    set_bounds();
}

void WebViewHost::PostResponse(const std::string& json) {
    if (!core_) return;
    core_->PostWebMessageAsJson(from_utf8(json).c_str());
}

HRESULT WebViewHost::on_web_message(ICoreWebView2WebMessageReceivedEventArgs* args) {
    if (!args) return S_OK;

    // WebView2 返回的字符串由 CoTaskMem 分配。
    // 注意：JS 侧 postMessage 传的是字符串，get_WebMessageAsJson 会把它再包一层引号
    // （"{\"cmd\":\"cpu\"}"），因此优先用 TryGetWebMessageAsString 取原文；
    // 若前端改为 postMessage(object)，它返回失败，再回退到 get_WebMessageAsJson。
    LPWSTR raw = nullptr;
    std::wstring text;
    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
        text = raw;
        CoTaskMemFree(raw);
        raw = nullptr;
    } else if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
        text = raw;
        CoTaskMemFree(raw);
        raw = nullptr;
    }
    if (text.empty()) return S_OK;

    std::wstring cmd;
    try {
        // 转成 UTF-8 字节再交给解析器
        const nlohmann::json j = nlohmann::json::parse(to_utf8(text));
        if (j.contains("cmd") && j["cmd"].is_string()) {
            cmd = from_utf8(j["cmd"].get<std::string>());
        }
    } catch (...) {
        PostResponse(build_error_response(L"", L"请求解析失败"));
        return S_OK;
    }

    if (cmd.empty()) {
        PostResponse(build_error_response(L"", L"请求缺少 cmd 字段"));
        return S_OK;
    }

    std::string response;
    if (handler_) response = handler_(cmd);
    // 空串 = 异步处理中（storage），结果稍后由 PostResponse 回传
    if (!response.empty()) PostResponse(response);
    return S_OK;
}

void WebViewHost::Destroy() {
    ready_ = false;
    handler_ = nullptr;

    if (core_) {
        core_->remove_WebMessageReceived(msgToken_);
        core_->remove_NavigationCompleted(navToken_);
    }
    core_.Reset();
    if (controller_) {
        controller_->Close();
        controller_.Reset();
    }
    env_.Reset();

    hwnd_ = nullptr;
    width_ = height_ = 0;
}

} // namespace desktopsticker::resmon
