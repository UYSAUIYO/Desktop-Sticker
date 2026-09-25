#include "pch.h"

#include "desktopsticker/IResMonModule.h"

#include "MemorySampler.h"
#include "ProcessSampler.h"
#include "ResMonWindow.h"
#include "StorageScanner.h"
#include "WebViewHost.h"

#include "desktopsticker/resmon/JsonBuild.h"

#include <condition_variable>

namespace desktopsticker {
namespace {

using namespace desktopsticker::resmon;

// 工作线程把扫描结果切回 UI 线程用的自定义消息（WebView2 只能在 UI 线程使用）
constexpr UINT kMsgStorageResult = WM_APP + 0x51;

std::wstring local_app_data_dir() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) return {};
    std::filesystem::path root(p);
    CoTaskMemFree(p);
    return root.wstring();
}

std::wstring log_path() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) return {};
    std::filesystem::path root(p);
    CoTaskMemFree(p);
    root /= L"DesktopSticker";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return (root / L"debug.log").wstring();
}

// 与 Features/WallPaper 写同一份 debug.log，按 [resmon] 标记来源
void rm_log(const std::string& msg) {
    const std::wstring path = log_path();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::app);
    out << "[resmon] " << msg << std::endl;
}

class ResMonModuleImpl final : public IResMonModule {
public:
    ~ResMonModuleImpl() override { Shutdown(); }

    bool Init(const ResMonPaths& paths) override {
        if (initialized_) return available_;
        paths_ = paths;

        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized_ = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) {
            rm_log("CoInitializeEx: thread already in another apartment model");
        }

        initialized_ = true;
        if (!create_window_and_webview()) {
            available_ = false;
            rm_log("unavailable (webview2 environment or window creation failed)");
            return false;
        }

        start_worker();
        available_ = true;
        rm_log("available");
        return true;
    }

    bool Show() override {
        if (!initialized_) return false;

        // 用户关过窗口：重建窗口与 WebView2（不做"隐藏"，与既有窗口语义一致）
        if (!window_.Exists() && !create_window_and_webview()) {
            available_ = false;
            rm_log("recreate on Show failed");
            return false;
        }
        window_.ShowAndFocus();
        return true;
    }

    void Shutdown() override {
        if (!initialized_) return;

        stop_worker(); // 必须先于销毁窗口：join 之后才不会再投递跨线程消息
        if (web_) {
            web_->Destroy();
            web_.reset();
        }
        window_.Destroy();
        available_ = false;
        initialized_ = false;
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
    }

    bool Available() override { return available_; }

private:
    bool create_window_and_webview() {
        window_.SetSizeHandler([this](int w, int h) {
            if (web_) web_->Resize(w, h);
        });
        window_.SetDestroyHandler([this]() {
            if (web_) web_->Destroy();
        });
        window_.SetAppMessageHandler([this](UINT msg, WPARAM, LPARAM lp) {
            return on_app_message(msg, lp);
        });

        if (!window_.Create()) {
            rm_log("ResMonWindow::Create failed");
            return false;
        }

        web_ = std::make_unique<WebViewHost>();
        const std::wstring userData =
            (std::filesystem::path(local_app_data_dir()) / L"DesktopSticker" / L"ResMonWebView")
                .wstring();
        const std::wstring assets = (std::filesystem::path(paths_.exeDir) / L"resmon").wstring();

        if (!web_->Create(window_.Handle(), assets, userData,
                          [this](const std::wstring& cmd) { return handle_command(cmd); })) {
            rm_log("WebViewHost::Create failed (webview2 runtime missing?)");
            web_.reset();
            window_.Destroy();
            return false;
        }
        return true;
    }

    std::string handle_command(const std::wstring& cmd) {
        if (cmd == L"cpu") {
            return build_cpu_response(sampler_.Sample(),
                                      static_cast<int64_t>(GetTickCount64()));
        }
        if (cmd == L"memory") {
            return build_memory_response(memory_.Sample(paths_.exeDir));
        }
        if (cmd == L"storage") {
            request_storage(); // 异步：重操作不阻塞 UI 线程
            return {};
        }
        return build_error_response(cmd, L"未知命令");
    }

    // ---- 存储扫描的工作线程（只保留最新一次请求的结果）----

    void request_storage() {
        {
            std::lock_guard<std::mutex> lock(reqMutex_);
            ++storageSeq_;
            storagePending_ = true;
        }
        reqCv_.notify_one();
    }

    void start_worker() {
        if (worker_.joinable()) return;
        workerQuit_ = false;
        worker_ = std::thread([this]() { worker_main(); });
    }

    void stop_worker() {
        if (!worker_.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(reqMutex_);
            workerQuit_ = true;
        }
        reqCv_.notify_all();
        worker_.join();
    }

    void worker_main() {
        for (;;) {
            uint64_t seq = 0;
            {
                std::unique_lock<std::mutex> lock(reqMutex_);
                reqCv_.wait(lock, [this] { return workerQuit_ || storagePending_; });
                if (workerQuit_) return;
                seq = storageSeq_;
                storagePending_ = false;
            }

            const StorageSnapshot snap = scanner_.Scan(paths_);
            std::string json = build_storage_response(snap);

            {
                std::lock_guard<std::mutex> lock(reqMutex_);
                if (seq != storageSeq_) continue; // 已有更新的请求，丢弃这次结果
            }

            auto* payload = new std::string(std::move(json));
            if (!PostMessageW(window_.Handle(), kMsgStorageResult, 0,
                              reinterpret_cast<LPARAM>(payload))) {
                delete payload;
            }
        }
    }

    bool on_app_message(UINT msg, LPARAM lp) {
        if (msg != kMsgStorageResult) return false;
        std::unique_ptr<std::string> payload(reinterpret_cast<std::string*>(lp));
        if (payload && web_) web_->PostResponse(*payload);
        return true;
    }

    ResMonPaths paths_;
    ResMonWindow window_;
    std::unique_ptr<WebViewHost> web_;
    ProcessSampler sampler_;
    MemorySampler memory_{ sampler_ };
    StorageScanner scanner_;

    bool initialized_ = false;
    bool available_ = false;
    bool comInitialized_ = false;

    std::thread worker_;
    std::mutex reqMutex_;
    std::condition_variable reqCv_;
    bool workerQuit_ = false;
    bool storagePending_ = false;
    uint64_t storageSeq_ = 0;
};

} // namespace
} // namespace desktopsticker

extern "C" DESKTOPSTICKER_RESMON_API desktopsticker::IResMonModule* CreateResMonModule() {
    try {
        return new desktopsticker::ResMonModuleImpl();
    } catch (...) {
        return nullptr;
    }
}

extern "C" DESKTOPSTICKER_RESMON_API void DestroyResMonModule(desktopsticker::IResMonModule* module) {
    delete module;
}
