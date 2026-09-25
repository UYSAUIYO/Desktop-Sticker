#pragma once

#include <cstdint>
#include <string>

namespace desktopsticker::wallpaper {

// 上屏路径：D3D11 设备 → DXGI 交换链（composition）→ DirectComposition visual，
// 帧内容用 D2D 绘制到交换链后缓冲（D2D 负责按 cover 方式缩放与宽高比处理）。
//
// 说明（实现取舍）：参考项目的呈现由自写着色器完成 NV12→RGB；本项目改为把解码结果
// 统一落到 BGRA，再用 D2D 的 DrawBitmap 缩放上屏。原因是 D2D 已是本项目成熟使用的栈
// （Features 的 ULW 管线就是 D2D），缩放/过滤/裁剪不用手写着色器，风险更低。
class D3dContext {
public:
    ~D3dContext();

    bool Create(HWND hwnd, int width, int height);
    void Destroy();
    bool Resize(int width, int height);

    // 上传一帧 BGRA（stride 为字节跨度）并按 cover 铺满窗口后呈现
    bool PresentBgra(const uint8_t* pixels, int width, int height, int stride);
    // 无视频时的纯色底
    bool Clear(float r, float g, float b);

    // 让位 / 复位：自呈现型后端（Web / Vulkan）接管窗口时，DComp visual 必须让位，
    // 否则会盖住它们。只置空并 Commit，**不释放设备**（切回来还要用）。
    void Suspend();
    void Resume();
    bool Suspended() const { return suspended_; }

    bool Valid() const { return targetBitmap_ != nullptr; }
    ID3D11Device* Device() const { return device_.Get(); }
    const char* LastError() const { return lastError_.c_str(); }

private:
    bool create_device_and_swapchain(HWND hwnd);
    bool create_dcomp(HWND hwnd);
    bool create_d2d_target();
    bool ensure_frame_bitmap(int width, int height);
    void fail(const char* stage, long hr);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> frameBitmap_;

    int width_ = 0;
    int height_ = 0;
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    bool suspended_ = false;
    std::string lastError_;
};

} // namespace desktopsticker::wallpaper
