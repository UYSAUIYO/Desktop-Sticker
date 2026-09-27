#pragma once

#include "WallpaperBackend.h"

namespace desktopsticker::wallpaper {

// .dswall 场景壁纸：由进程内嵌的 Godot 运行库渲染（无外部 Godot 程序、无独立渲染窗口）。
// 自呈现型：引擎在自己的交换链上呈现到宿主提供的壁纸窗口，D3dContext 让位。
class GodotEmbeddedBackend final : public IWallpaperBackend {
public:
    ~GodotEmbeddedBackend() override { Close(); }

    bool Open(const BackendRequest& request, const BackendContext& ctx) override;
    void Close() override;

    bool SelfPresenting() const override { return true; }
    bool ProduceFrame(VideoFrame&) override { return false; }
    void Tick() override;
    void SetPaused(bool paused) override;
    void SetSpeed(double) override {}
    double TargetFps() const override { return 60.0; }
    const char* Name() const override { return "Godot Scene"; }
    BackendKind Kind() const override { return BackendKind::Shader3D; }
    uint32_t FrameSerial() const override { return serial_; }

private:
    uint32_t serial_ = 0;
};

} // namespace desktopsticker::wallpaper
