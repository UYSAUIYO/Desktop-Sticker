#include "pch.h"
#include "WallpaperBackend.h"

#include "GodotEmbeddedBackend.h"
#include "GodotEngineHost.h"
#include "ImageSequenceBackend.h"
#include "Log.h"
#include "Utf8.h"
#include "VideoBackend.h"
#include "VulkanBackend.h"
#include "WebBackend.h"

#include <filesystem>

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

// 已解包的 .dswall 壁纸包目录（media/<id>/shader 里有 project.pck）。
bool is_embedded_pack_dir(const std::wstring& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(dir) / L"project.pck", ec);
}

} // namespace

// 后端工厂。Shader3D 按目录内容分派：project.pck 在 → 内嵌 Godot 场景后端，
// 否则 Vulkan 内置场景（两种 3D 内容形态共存，互为降级）。
std::unique_ptr<IWallpaperBackend> create_backend(BackendKind kind,
                                                  const std::wstring& sourcePath) {
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
            if (is_embedded_pack_dir(sourcePath)) {
                if (!GodotEngineHost::instance().runtime_available()) {
                    wp_log("godot scene requested but the embedded runtime is missing (exeDir\\godot)");
                    return nullptr;
                }
                return std::make_unique<GodotEmbeddedBackend>();
            }
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
            // 内嵌 Godot 场景与 Vulkan 内置场景共用一个 kind：任一可用即可。
            return vulkan_available() || GodotEngineHost::instance().runtime_available();
    }
    return false;
}

} // namespace desktopsticker::wallpaper
