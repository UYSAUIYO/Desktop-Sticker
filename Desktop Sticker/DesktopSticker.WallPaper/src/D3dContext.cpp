#include "pch.h"
#include "D3dContext.h"

#include "Log.h"
#include "desktopsticker/wallpaper/CoverRect.h"

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

namespace {

// NV12 → RGB 的全屏三角形。用运行时 D3DCompile 而不是构建期 fxc：本工程没有着色器
// 构建步骤，多一个自定义 Build 步骤只为两段很短的 HLSL 不划算。
// 注意 D3D 的 NDC 是 y 向上、纹理 v 向下，所以 uv.y 要取反。
const char* kNv12ShaderSrc = R"(
cbuffer Params : register(b0) {
    float2 uvScale;
    float2 uvOffset;
};
Texture2D<float>  texY  : register(t0);
Texture2D<float2> texUV : register(t1);
SamplerState samp : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);
    return o;
}

float4 ps_main(VSOut i) : SV_Target {
    float2 t = i.uv * uvScale + uvOffset;
    float y  = texY.Sample(samp, t).r;
    float2 c = texUV.Sample(samp, t).rg;
    // BT.709 有限范围（16-235 / 16-240）：本工程面向的是常见 HD 素材
    float Y  = (y - 16.0 / 255.0) * (255.0 / 219.0);
    float Cb = (c.x - 128.0 / 255.0) * (255.0 / 224.0);
    float Cr = (c.y - 128.0 / 255.0) * (255.0 / 224.0);
    float3 rgb = float3(Y + 1.5748 * Cr,
                        Y - 0.1873 * Cb - 0.4681 * Cr,
                        Y + 1.8556 * Cb);
    return float4(saturate(rgb), 1.0);
}
)";

struct Nv12Params {
    float uvScale[2];
    float uvOffset[2];
};

} // namespace

D3dContext::~D3dContext() {
    Destroy();
}

void D3dContext::fail(const char* stage, long hr) {
    lastError_ = std::string(stage) + " failed hr=0x" + [] (long v) {
        char buf[16]{};
        snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(v));
        return std::string(buf);
    }(hr);
    wp_log("D3dContext: " + lastError_);
}

bool D3dContext::create_device_and_swapchain(HWND hwnd) {
    // VIDEO_SUPPORT 是硬解的硬前提：MF 的 DXVA 解码器与视频处理器要求设备按视频用途创建；
    // 缺了它，硬件 MFT 初始化走不通或行为未定义。
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, 2, D3D11_SDK_VERSION,
                                   &device_, nullptr, &context_);
    if (FAILED(hr)) {
        wp_log("D3dContext: hardware device unavailable, falling back to WARP");
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, 2, D3D11_SDK_VERSION,
                               &device_, nullptr, &context_);
    }
    if (FAILED(hr)) { fail("D3D11CreateDevice", hr); return false; }

    // 立即上下文将同时被渲染线程与我们自己的线程使用（MF 的 DXVA/视频处理器在其工作线程
    // 上通过 IMFDXGIDeviceManager 触达同一个设备）。没有这把设备级锁，两条线程会裸并发
    // 踩对方的 GPU 状态：现象是"播几帧后 ReadSample 永久阻塞 + 画面黑/花"——实测如此。
    Microsoft::WRL::ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(context_.As(&mt)) && mt) {
        mt->SetMultithreadProtected(TRUE);
    } else {
        wp_log("D3dContext: ID3D11Multithread unavailable; hardware decode may deadlock");
    }

    // NV12 的平面 SRV 只有 desc1 能表达（基础 d3d11.h 的 SRV desc 没有 PlaneSlice）。
    // 拿不到就当 GPU 上屏不可用，硬解路径会因此不启用。
    if (FAILED(device_.As(&device3_)) || !device3_) {
        wp_log("D3dContext: ID3D11Device3 unavailable; NV12 GPU present disabled");
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                  reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    const HRESULT scHr = factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr,
                                                               &swapChain_);
    if (FAILED(scHr)) { fail("CreateSwapChainForComposition", scHr); return false; }
    return true;
}

bool D3dContext::create_dcomp(HWND hwnd) {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) return false;

    const HRESULT devHr = DCompositionCreateDevice(dxgiDevice.Get(),
                                                   __uuidof(IDCompositionDevice),
                                                   reinterpret_cast<void**>(dcompDevice_.GetAddressOf()));
    if (FAILED(devHr)) { fail("DCompositionCreateDevice", devHr); return false; }

    HRESULT hr = dcompDevice_->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget_);
    if (FAILED(hr)) { fail("CreateTargetForHwnd", hr); return false; }

    hr = dcompDevice_->CreateVisual(&dcompVisual_);
    if (FAILED(hr)) { fail("CreateVisual", hr); return false; }

    hr = dcompVisual_->SetContent(swapChain_.Get());
    if (FAILED(hr)) { fail("SetContent", hr); return false; }

    hr = dcompTarget_->SetRoot(dcompVisual_.Get());
    if (FAILED(hr)) { fail("SetRoot", hr); return false; }

    hr = dcompDevice_->Commit();
    if (FAILED(hr)) { fail("Commit", hr); return false; }
    return true;
}

bool D3dContext::create_d2d_target() {
    if (!d2dFactory_) {
        D2D1_FACTORY_OPTIONS options{};
        const HRESULT fHr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                              __uuidof(ID2D1Factory1), &options,
                                              reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
        if (FAILED(fHr)) { fail("D2D1CreateFactory", fHr); return false; }
    }

    if (!d2dDevice_) {
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device_.As(&dxgiDevice))) return false;
        const HRESULT dHr = d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_);
        if (FAILED(dHr)) { fail("ID2D1Factory1::CreateDevice", dHr); return false; }
    }

    if (!d2dContext_) {
        const HRESULT cHr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                            &d2dContext_);
        if (FAILED(cHr)) { fail("CreateDeviceContext", cHr); return false; }
    }

    ComPtr<IDXGISurface> backBuffer;
    const HRESULT bbHr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface),
                                              reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(bbHr)) { fail("GetBuffer", bbHr); return false; }

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f, 96.0f);

    targetBitmap_.Reset();
    const HRESULT btHr = d2dContext_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &props,
                                                                 &targetBitmap_);
    if (FAILED(btHr)) { fail("CreateBitmapFromDxgiSurface", btHr); return false; }

    d2dContext_->SetTarget(targetBitmap_.Get());

    // GPU 路径要直接画到后缓冲，所以顺便建一个 RTV（与 D2D 目标同一张纹理；
    // 两条路径不会同时用，各自在开始前重新声明自己的绑定）
    backBufferRtv_.Reset();
    ComPtr<ID3D11Texture2D> backTexture;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backTexture))) ||
        FAILED(device_->CreateRenderTargetView(backTexture.Get(), nullptr, &backBufferRtv_))) {
        wp_log("D3dContext: back-buffer RTV unavailable; GPU present disabled");
        backBufferRtv_.Reset();
    }
    return true;
}

bool D3dContext::Create(HWND hwnd, int width, int height) {
    Destroy();
    width_ = width > 0 ? width : 1920;
    height_ = height > 0 ? height : 1080;

    if (!create_device_and_swapchain(hwnd)) { Destroy(); return false; }
    if (!create_dcomp(hwnd)) { Destroy(); return false; }
    if (!create_d2d_target()) { Destroy(); return false; }
    return true;
}

void D3dContext::Destroy() {
    suspended_ = false;
    nv12Luma_.Reset();
    nv12Chroma_.Reset();
    nv12Copy_.Reset();
    nv12CopyWidth_ = nv12CopyHeight_ = 0;
    device3_.Reset();
    nv12Sampler_.Reset();
    nv12Rs_.Reset();
    nv12Params_.Reset();
    nv12Ps_.Reset();
    nv12Vs_.Reset();
    backBufferRtv_.Reset();
    frameBitmap_.Reset();
    frameWidth_ = frameHeight_ = 0;
    if (d2dContext_) d2dContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    // 先撤掉 DComp 目标再释放交换链，避免合成器持有已释放资源
    if (dcompTarget_) dcompTarget_->SetRoot(nullptr);
    if (dcompDevice_) dcompDevice_->Commit();
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

bool D3dContext::Resize(int width, int height) {
    if (!swapChain_ || width <= 0 || height <= 0) return false;
    if (width == width_ && height == height_) return true;

    if (d2dContext_) d2dContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
    frameBitmap_.Reset();
    frameWidth_ = frameHeight_ = 0;
    backBufferRtv_.Reset();   // DXGI 要求 ResizeBuffers 前释放后缓冲的全部引用

    const HRESULT hr = swapChain_->ResizeBuffers(0, static_cast<UINT>(width),
                                                 static_cast<UINT>(height),
                                                 DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) { fail("ResizeBuffers", hr); return false; }

    width_ = width;
    height_ = height;
    return create_d2d_target();
}

bool D3dContext::ensure_frame_bitmap(int width, int height) {
    if (frameBitmap_ && frameWidth_ == width && frameHeight_ == height) return true;

    frameBitmap_.Reset();
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);

    const HRESULT hr = d2dContext_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width),
                                                             static_cast<UINT32>(height)),
                                                 nullptr, 0, props, &frameBitmap_);
    if (FAILED(hr)) { fail("CreateBitmap", hr); return false; }

    frameWidth_ = width;
    frameHeight_ = height;
    return true;
}

void D3dContext::Suspend() {
    if (!dcompVisual_ || suspended_) return;
    dcompVisual_->SetContent(nullptr);
    if (dcompDevice_) dcompDevice_->Commit();
    suspended_ = true;
    wp_log("D3dContext: suspended (self-presenting backend takes over)");
}

void D3dContext::Resume() {
    if (!suspended_ || !dcompVisual_ || !swapChain_) return;
    dcompVisual_->SetContent(swapChain_.Get());
    if (dcompDevice_) dcompDevice_->Commit();
    suspended_ = false;
    wp_log("D3dContext: resumed");
}

bool D3dContext::SetRootVisual(IDCompositionVisual* visual) {
    if (!dcompTarget_ || !dcompDevice_ || !visual) return false;
    const HRESULT hr = dcompTarget_->SetRoot(visual);
    if (FAILED(hr)) { fail("SetRoot(web visual)", hr); return false; }
    return SUCCEEDED(dcompDevice_->Commit());
}

void D3dContext::RestoreRootVisual() {
    if (!dcompTarget_ || !dcompDevice_ || !dcompVisual_) return;
    dcompTarget_->SetRoot(dcompVisual_.Get());
    dcompDevice_->Commit();
}

bool D3dContext::ensure_nv12_pipeline() {
    if (nv12Vs_ && nv12Ps_ && nv12Params_ && nv12Sampler_ && nv12Rs_) return true;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> err;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(kNv12ShaderSrc, std::strlen(kNv12ShaderSrc), "nv12", nullptr, nullptr,
                          "vs_main", "vs_4_0", flags, 0, &vsBlob, &err))) {
        lastError_ = std::string("D3DCompile(vs_main) failed: ") +
                     (err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        wp_log("D3dContext: " + lastError_);
        return false;
    }
    if (FAILED(D3DCompile(kNv12ShaderSrc, std::strlen(kNv12ShaderSrc), "nv12", nullptr, nullptr,
                          "ps_main", "ps_4_0", flags, 0, &psBlob, &err))) {
        lastError_ = std::string("D3DCompile(ps_main) failed: ") +
                     (err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        wp_log("D3dContext: " + lastError_);
        return false;
    }

    if (FAILED(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                           nullptr, &nv12Vs_))) {
        fail("CreateVertexShader(nv12)", E_FAIL);
        return false;
    }
    if (FAILED(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                          nullptr, &nv12Ps_))) {
        fail("CreatePixelShader(nv12)", E_FAIL);
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Nv12Params);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&cb, nullptr, &nv12Params_))) {
        fail("CreateBuffer(nv12 params)", E_FAIL);
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;   // 裁剪式缩放不能让它环绕
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, &nv12Sampler_))) {
        fail("CreateSamplerState(nv12)", E_FAIL);
        return false;
    }

    // 全屏三角形的顶点 (-1,-1)->(3,-1)->(-1,3) 在 NDC 里是逆时针，默认光栅化状态
    // （CULL BACK + 顺时针为正面）会把它整面剔除——Draw 正常返回但一个像素都不落，
    // 后缓冲保持黑色：这就是 mf-d3d11 "帧率满格、画面全黑" 的根因。必须显式关剔除。
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, &nv12Rs_))) {
        fail("CreateRasterizerState(nv12)", E_FAIL);
        return false;
    }
    wp_log("D3dContext: NV12 GPU present pipeline ready");
    return true;
}

bool D3dContext::ensure_nv12_copy(int width, int height) {
    if (nv12Copy_ && nv12CopyWidth_ == width && nv12CopyHeight_ == height) return true;
    if (!device3_) return false;

    nv12Luma_.Reset();
    nv12Chroma_.Reset();
    nv12Copy_.Reset();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &nv12Copy_))) {
        fail("CreateTexture2D(nv12 copy)", E_FAIL);
        return false;
    }

    // NV12 的两个平面各建一个 SRV，平面靠 desc1 的 PlaneSlice 指定
    const DXGI_FORMAT planes[2] = { DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM };
    ComPtr<ID3D11ShaderResourceView>* slots[2] = { &nv12Luma_, &nv12Chroma_ };
    for (int i = 0; i < 2; ++i) {
        D3D11_SHADER_RESOURCE_VIEW_DESC1 srv{};
        srv.Format = planes[i];
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = static_cast<UINT>(i);
        ComPtr<ID3D11ShaderResourceView1> view1;
        if (FAILED(device3_->CreateShaderResourceView1(nv12Copy_.Get(), &srv, &view1)) ||
            FAILED(view1.As(slots[i]))) {
            fail("CreateShaderResourceView1(nv12 plane)", E_FAIL);
            nv12Copy_.Reset();
            return false;
        }
    }

    nv12CopyWidth_ = width;
    nv12CopyHeight_ = height;
    wp_log("D3dContext: NV12 staging texture " + std::to_string(width) + "x" +
           std::to_string(height));
    return true;
}

bool D3dContext::PresentNv12(ID3D11Texture2D* texture, uint32_t subresource, int width,
                             int height) {
    if (!targetBitmap_ || suspended_ || !texture || width <= 0 || height <= 0) return false;
    if (!backBufferRtv_) return false;
    if (!ensure_nv12_pipeline()) return false;
    if (!ensure_nv12_copy(width, height)) return false;

    // 解码器的纹理不能直接采样，先拷进我们自己的 NV12 纹理（GPU 侧，不落内存）
    context_->CopySubresourceRegion(nv12Copy_.Get(), 0, 0, 0, 0, texture, subresource, nullptr);

    // cover：不动几何，改采样坐标（见 CoverRect.h）
    const UvRect uv = cover_uv_rect(width, height, width_, height_);
    Nv12Params params{};
    params.uvScale[0] = static_cast<float>(uv.scaleX);
    params.uvScale[1] = static_cast<float>(uv.scaleY);
    params.uvOffset[0] = static_cast<float>(uv.offsetX);
    params.uvOffset[1] = static_cast<float>(uv.offsetY);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(nv12Params_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, &params, sizeof(params));
    context_->Unmap(nv12Params_.Get(), 0);

    // D2D 目标先让位：同一张后缓冲不能同时被 D2D 与裸 D3D 绘制
    d2dContext_->SetTarget(nullptr);

    const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(width_),
                                   static_cast<float>(height_), 0.0f, 1.0f };
    ID3D11RenderTargetView* rtv = backBufferRtv_.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(nv12Rs_.Get());   // CullNone：见 ensure_nv12_pipeline 里的注释
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[] = { nv12Params_.Get() };
    context_->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = { nv12Luma_.Get(), nv12Chroma_.Get() };
    context_->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* samplers[] = { nv12Sampler_.Get() };
    context_->PSSetSamplers(0, 1, samplers);
    context_->VSSetShader(nv12Vs_.Get(), nullptr, 0);
    context_->PSSetShader(nv12Ps_.Get(), nullptr, 0);
    context_->Draw(3, 0);

    // 解绑：下一帧 SRV 可能被解码器回收重建
    ID3D11ShaderResourceView* none[2] = { nullptr, nullptr };
    context_->PSSetShaderResources(0, 2, none);

    const HRESULT pr = swapChain_->Present(1, 0);
    if (FAILED(pr)) { fail("Present(nv12)", pr); return false; }
    return true;
}

bool D3dContext::Clear(float r, float g, float b) {
    if (!targetBitmap_ || suspended_) return false;

    d2dContext_->SetTarget(targetBitmap_.Get());   // GPU 路径可能把它置空过
    d2dContext_->BeginDraw();
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->Clear(D2D1::ColorF(r, g, b, 1.0f));
    const HRESULT hr = d2dContext_->EndDraw();
    if (FAILED(hr)) { fail("Clear EndDraw", hr); return false; }

    swapChain_->Present(1, 0);
    return true;
}

bool D3dContext::PresentBgra(const uint8_t* pixels, int width, int height, int stride) {
    if (!targetBitmap_ || suspended_ || !pixels || width <= 0 || height <= 0) return false;
    if (!ensure_frame_bitmap(width, height)) return false;

    d2dContext_->SetTarget(targetBitmap_.Get());   // GPU 路径可能把它置空过

    const HRESULT cp = frameBitmap_->CopyFromMemory(nullptr, pixels, static_cast<UINT32>(stride));
    if (FAILED(cp)) { fail("CopyFromMemory", cp); return false; }

    // cover：保持宽高比铺满窗口，超出部分由 D2D 的渲染目标边界裁掉
    const float scale = std::max(static_cast<float>(width_) / width,
                                 static_cast<float>(height_) / height);
    const float dstW = width * scale;
    const float dstH = height * scale;
    const D2D1_RECT_F dest = D2D1::RectF((width_ - dstW) * 0.5f, (height_ - dstH) * 0.5f,
                                         (width_ + dstW) * 0.5f, (height_ + dstH) * 0.5f);

    d2dContext_->BeginDraw();
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->DrawBitmap(frameBitmap_.Get(), &dest, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR,
                            nullptr);
    const HRESULT hr = d2dContext_->EndDraw();
    if (FAILED(hr)) { fail("Present EndDraw", hr); return false; }

    const HRESULT pr = swapChain_->Present(1, 0);
    if (FAILED(pr)) { fail("Present", pr); return false; }
    return true;
}

} // namespace desktopsticker::wallpaper
