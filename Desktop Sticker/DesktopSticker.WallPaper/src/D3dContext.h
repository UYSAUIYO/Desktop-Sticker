#pragma once

#include <cstdint>
#include <map>
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
    // 直接画硬件解码出来的 NV12 纹理（GPU 色彩转换 + cover 缩放，全程不落回内存）
    bool PresentNv12(ID3D11Texture2D* texture, uint32_t subresource, int width, int height);
    // 无视频时的纯色底
    bool Clear(float r, float g, float b);

    // 让位 / 复位：自呈现型后端（Web / Vulkan）接管窗口时，DComp visual 必须让位，
    // 否则会盖住它们。只置空并 Commit，**不释放设备**（切回来还要用）。
    void Suspend();
    void Resume();
    bool Suspended() const { return suspended_; }

    // ③ Web 后端把 WebView2 的画面合成进**我们的** DComp 树（视觉宿主）：
    // 它需要把自己的视觉挂成根。让位之后我们自己的视觉内容已撤空，所以直接换根即可，
    // 不需要额外做一层容器视觉。
    IDCompositionDevice* DCompDevice() const { return dcompDevice_.Get(); }
    bool SetRootVisual(IDCompositionVisual* visual);
    void RestoreRootVisual();

    // 能否把 NV12 纹理直接画上屏。硬解前必须先问这个：开了硬解却画不出来会变成"黑屏不动"
    bool Nv12Available() const { return device3_ && backBufferRtv_; }

    bool Valid() const { return targetBitmap_ != nullptr; }
    ID3D11Device* Device() const { return device_.Get(); }
    const char* LastError() const { return lastError_.c_str(); }

private:
    bool create_device_and_swapchain(HWND hwnd);
    bool create_dcomp(HWND hwnd);
    bool create_d2d_target();
    bool ensure_frame_bitmap(int width, int height);
    // NV12 上屏的 D3D11 管线（着色器运行时编译一次；SRV 建在**我们自己的**纹理上）
    bool ensure_nv12_pipeline();
    bool ensure_nv12_copy(int width, int height);
    void fail(const char* stage, long hr);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Device3> device3_;   // NV12 的 SRV 需要 desc1
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

    // 交换链后缓冲的 RTV：GPU 路径直接画，不经过 D2D
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRtv_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> nv12Vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> nv12Ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> nv12Params_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> nv12Sampler_;
    // 全屏三角形必须用 CullNone（默认光栅化状态会把它整面剔除，见 D3dContext.cpp）
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> nv12Rs_;
    // 解码器给的纹理**不能直接建 SRV**（实测 CreateShaderResourceView1 返回 E_FAIL：
    // 它不是按 SHADER_RESOURCE 用途建的），所以先拷进我们自己的 NV12 纹理再采样。
    // 一次 GPU 侧拷贝，4K 也就十几 MB，比走 CPU 便宜得多。
    Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12Copy_;
    int nv12CopyWidth_ = 0;
    int nv12CopyHeight_ = 0;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nv12Luma_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nv12Chroma_;

    int width_ = 0;
    int height_ = 0;
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    bool suspended_ = false;
    std::string lastError_;
};

} // namespace desktopsticker::wallpaper
