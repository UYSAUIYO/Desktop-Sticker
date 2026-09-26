#pragma once

#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "WallpaperBackend.h"
#include "desktopsticker/wallpaper/FrameAdvance.h"

namespace desktopsticker::wallpaper {

// 图片序列后端（②）：media/<id>/frames/ 里的图片按自然序逐帧播放。
// 单张静态图片也走这条路径（序列长度 1），免得再为它写一个后端。
//
// 与视频后端的差别：序列没有自带帧率，节奏完全由我们给（默认 100ms/帧，见
// kSequenceFrameMs），所以调速直接缩放这个间隔，不存在"源帧率"一说。
class ImageSequenceBackend final : public IWallpaperBackend {
public:
    ~ImageSequenceBackend() override { Close(); }

    bool Open(const BackendRequest& request, const BackendContext& ctx) override;
    void Close() override;

    bool SelfPresenting() const override { return false; }
    bool ProduceFrame(VideoFrame& out) override;
    void SetPaused(bool paused) override;
    void SetSpeed(double speed) override;
    double TargetFps() const override;
    uint32_t FrameSerial() const override { return serial_; }

    const char* Name() const override { return "图片序列"; }
    BackendKind Kind() const override { return BackendKind::ImageSequence; }
    const char* LastError() const { return lastError_.c_str(); }

private:
    int64_t qpc_us() const;
    // 解码 frames_[index_] 到 bgra；失败返回 false
    bool decode_current(std::vector<uint8_t>& bgra, int& w, int& h);

    std::vector<std::wstring> frames_;
    size_t index_ = 0;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    int maxW_ = 0;
    int maxH_ = 0;
    int lastW_ = 0;
    int lastH_ = 0;
    uint32_t serial_ = 0;
    int64_t lastTickUs_ = 0;
    FrameAdvanceState advanceState_;
    double speed_ = 1.0;
    bool paused_ = false;
    std::string lastError_;
};

} // namespace desktopsticker::wallpaper
