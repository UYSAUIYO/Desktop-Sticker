#include "pch.h"
#include "WallpaperBackend.h"

#include "ImageSequenceBackend.h"
#include "Log.h"
#include "VideoBackend.h"
#include "VulkanBackend.h"
#include "WebBackend.h"

namespace desktopsticker::wallpaper {

namespace {

// WebView2 运行时是否装了：没有就只让 ③ 不可用，其余后端不受影响。
// 用 loader 的版本查询而不是"试着建环境" —— 后者要建进程、还得分派回调。
bool web_runtime_available() {
    LPWSTR version = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    if (version) CoTaskMemFree(version);
    return SUCCEEDED(hr);
}

} // namespace

// 后端工厂。④ 3D/着色器尚未接入（Vulkan 负载未落地），返回 nullptr 让调用方按"该类型不可用"降级。
std::unique_ptr<IWallpaperBackend> create_backend(BackendKind kind) {
    switch (kind) {
        case BackendKind::Video:
        case BackendKind::AnimatedImage:
            return std::make_unique<VideoBackend>();
        case BackendKind::ImageSequence:
            return std::make_unique<ImageSequenceBackend>();
        case BackendKind::Web:
            if (!web_runtime_available()) {
                wp_log("web backend requested but the WebView2 runtime is not installed");
                return nullptr;
            }
            return std::make_unique<WebBackend>();
        case BackendKind::Shader3D:
            if (!vulkan_available()) {
                wp_log("shader backend requested but no usable Vulkan device was found");
                return nullptr;
            }
            return std::make_unique<VulkanBackend>();
    }
    return nullptr;
}

bool backend_available(BackendKind kind) {
    switch (kind) {
        case BackendKind::Video:
        case BackendKind::AnimatedImage:
        case BackendKind::ImageSequence:
            return true;
        case BackendKind::Web:
            return web_runtime_available();
        case BackendKind::Shader3D:
            return vulkan_available();
    }
    return false;
}

} // namespace desktopsticker::wallpaper
