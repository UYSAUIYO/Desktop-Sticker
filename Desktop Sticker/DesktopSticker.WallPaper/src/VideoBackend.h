#pragma once

#include "VideoSource.h"
#include "WallpaperBackend.h"
#include "desktopsticker/wallpaper/FrameAdvance.h"

namespace desktopsticker::wallpaper {

// 视频 / 动图后端（① ②）：复用既有 IVideoSource（MF 为主 + FFmpeg 兜底），
// 在其上实现调速（丢帧追赶 / 保持帧）与音轨接线。
class VideoBackend final : public IWallpaperBackend {
public:
    ~VideoBackend() override { Close(); }

    bool Open(const BackendRequest& request, const BackendContext& ctx) override;
    void Close() override;

    bool SelfPresenting() const override { return false; }
    bool ProduceFrame(std::vector<uint8_t>& bgra, int& w, int& h) override;
    void SetPaused(bool paused) override;
    void SetSpeed(double speed) override;
    double TargetFps() const override;

    const char* Name() const override { return name_.c_str(); }
    BackendKind Kind() const override { return kind_; }

private:
    int64_t qpc_us() const;

    std::unique_ptr<IVideoSource> source_;
    // 保持帧时只回上次的尺寸、**不动调用方的缓冲区**（里面已经是上一帧），
    // 避免为留副本而每帧拷贝一次整帧 —— 4K 一帧 33MB，那是每帧十几毫秒的纯浪费。
    int lastW_ = 0;
    int lastH_ = 0;
    int64_t lastTickUs_ = 0;
    // 跨调用累积的帧余量：调度节拍有抖动，不累积就会持续丢帧（见 FrameAdvance.h）
    FrameAdvanceState advanceState_;
    double speed_ = 1.0;
    bool paused_ = false;
    BackendKind kind_ = BackendKind::Video;
    std::string name_ = "视频";
    AudioEngine* audio_ = nullptr;
};

} // namespace desktopsticker::wallpaper
