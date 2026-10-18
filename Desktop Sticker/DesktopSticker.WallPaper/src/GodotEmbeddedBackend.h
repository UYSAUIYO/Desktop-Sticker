#pragma once

#include "WallpaperBackend.h"

namespace desktopsticker::wallpaper {

// .dswall 场景壁纸：由进程内嵌的 Godot 运行库渲染（无外部 Godot 程序、无独立渲染窗口）。
// 自呈现型：引擎在自己的交换链上呈现到宿主提供的壁纸窗口，D3dContext 让位。
// 鼠标：引擎嵌入时窗口过程被 fork 换成了薄壳（见 dstk_embed_windows.cpp），
//       宿主把鼠标消息 PostMessage 过去，引擎按原生输入路径处理（含 SetCapture）。
class GodotEmbeddedBackend final : public IWallpaperBackend, public IMouseSink {
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

    void OnRelayMouse(const MouseRelayEvent& event) override;

private:
    uint32_t serial_ = 0;
    MouseRelay* relay_ = nullptr;
};

} // namespace desktopsticker::wallpaper
