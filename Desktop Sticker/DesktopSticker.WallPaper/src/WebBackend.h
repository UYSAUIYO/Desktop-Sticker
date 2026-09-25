#pragma once

#include <wrl/client.h>

#include <functional>
#include <string>

#include "WallpaperBackend.h"

namespace desktopsticker::wallpaper {

// Web 壁纸后端（③）：WebView2 走**视觉宿主**（visual hosting）—— 页面画面由 WebView2
// 挂到我们自己的 DComp 视觉树上，再由 DComp 合成到壁纸窗口。
//
// 为什么不是"自己开窗口、WebView2 自己画"：实测（2026-09-25）窗口化宿主在"嵌入 WorkerW
// 的壁纸窗口"里**不合成** —— 环境/控制器/导航/title/窗口树/可见性/尺寸全部正常，屏幕上
// 一个像素都没有（用纯红测试页验证过）；同一个窗口改成顶层就正常，说明问题出在桌面嵌入
// 这一层，不是调用序列。而本项目自己的 DComp 在同一种窗口上一直是好的，所以改走视觉宿主。
//
// 其它刻意的选择：
//   · 页面跑在 WebView2 自带的沙箱进程里，**不注入任何宿主对象**；只能通过虚拟主机
//     访问自己条目目录下的 web\ 资源。
//   · DevTools / 右键菜单 / 状态栏 / 快捷键一律关掉 —— 这是壁纸，不是浏览器。
//   · 不吃输入（壁纸窗口本就 HTTRANSPARENT），因此不接 SendMouseInput。
//   · 调速不适用：页面动画节奏由它自己的 requestAnimationFrame 决定，我们唯一的杠杆
//     是挂起（暂停时 TrySuspend 真正释放 CPU/GPU）。
class WebBackend final : public IWallpaperBackend {
public:
    ~WebBackend() override { Close(); }

    bool Open(const BackendRequest& request, const BackendContext& ctx) override;
    void Close() override;

    // 内容由 WebView2 经 DComp 画，调用方不调用 ProduceFrame，只每轮 Tick
    bool SelfPresenting() const override { return true; }
    bool ProduceFrame(std::vector<uint8_t>&, int&, int&) override { return false; }
    void Tick() override;

    void SetPaused(bool paused) override;
    // 网页的动画节奏由页面自己决定，我们无法介入（规格 §11 据此禁用该控件）
    void SetSpeed(double) override {}
    double TargetFps() const override { return 60.0; }

    const char* Name() const override { return "网页"; }
    BackendKind Kind() const override { return BackendKind::Web; }
    const char* LastError() const { return lastError_.c_str(); }

private:
    void apply_bounds();
    void apply_audio();
    void suspend(bool want);

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> composition_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> core_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    IDCompositionDevice* dcompDevice_ = nullptr;
    std::function<void()> restoreRoot_;
    AudioEngine* audio_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool muted_ = true;
    bool paused_ = false;
    std::string lastError_;
};

} // namespace desktopsticker::wallpaper
