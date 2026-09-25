#pragma once

#include <functional>
#include <string>

namespace desktopsticker::resmon {

// WebView2 宿主：环境、控制器、虚拟主机映射与 postMessage 消息桥。
// 全部方法必须在创建它的那个线程（EXE UI 线程）调用。
class WebViewHost {
public:
    // 返回该命令的响应 JSON；返回空串表示"异步处理中"（稍后由 PostResponse 回传）
    using CommandHandler = std::function<std::string(const std::wstring& cmd)>;

    ~WebViewHost();

    // userDataDir 由调用方给出（规格要求放 %LOCALAPPDATA%，不污染程序目录）
    bool Create(HWND hwnd, const std::wstring& assetsDir, const std::wstring& userDataDir,
                CommandHandler handler);
    void Destroy();
    bool Ready() const { return ready_; }

    void Resize(int width, int height);
    void PostResponse(const std::string& json);

private:
    HRESULT on_web_message(ICoreWebView2WebMessageReceivedEventArgs* args);
    void set_bounds();

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> core_;

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;
    std::wstring assetsDir_;
    CommandHandler handler_;
    EventRegistrationToken msgToken_{};
    EventRegistrationToken navToken_{};
};

} // namespace desktopsticker::resmon
