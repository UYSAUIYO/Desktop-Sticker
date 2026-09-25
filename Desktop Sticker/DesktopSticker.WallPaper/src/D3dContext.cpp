#include "pch.h"
#include "D3dContext.h"

#include "Log.h"

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

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
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
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

bool D3dContext::Clear(float r, float g, float b) {
    if (!targetBitmap_) return false;

    d2dContext_->BeginDraw();
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->Clear(D2D1::ColorF(r, g, b, 1.0f));
    const HRESULT hr = d2dContext_->EndDraw();
    if (FAILED(hr)) { fail("Clear EndDraw", hr); return false; }

    swapChain_->Present(1, 0);
    return true;
}

bool D3dContext::PresentBgra(const uint8_t* pixels, int width, int height, int stride) {
    if (!targetBitmap_ || !pixels || width <= 0 || height <= 0) return false;
    if (!ensure_frame_bitmap(width, height)) return false;

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
