#include "pch.h"
#include "WallpaperBackend.h"

#include "VideoBackend.h"

namespace desktopsticker::wallpaper {

// 后端工厂。目前接入了产帧型的视频/动图后端；
// 图片序列、Web、3D 着色器尚未接入，返回 nullptr 让调用方按"该类型不可用"降级。
std::unique_ptr<IWallpaperBackend> create_backend(BackendKind kind) {
    switch (kind) {
        case BackendKind::Video:
        case BackendKind::AnimatedImage:
            return std::make_unique<VideoBackend>();
        case BackendKind::ImageSequence:
        case BackendKind::Web:
        case BackendKind::Shader3D:
            return nullptr;
    }
    return nullptr;
}

bool backend_available(BackendKind kind) {
    switch (kind) {
        case BackendKind::Video:
        case BackendKind::AnimatedImage:
            return true;
        case BackendKind::ImageSequence:
        case BackendKind::Web:
        case BackendKind::Shader3D:
            return false;
    }
    return false;
}

} // namespace desktopsticker::wallpaper
