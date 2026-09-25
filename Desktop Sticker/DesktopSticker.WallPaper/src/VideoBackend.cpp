#include "pch.h"
#include "VideoBackend.h"

#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/FrameAdvance.h"

namespace desktopsticker::wallpaper {

int64_t VideoBackend::qpc_us() const {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000LL / freq;
}

bool VideoBackend::Open(const BackendRequest& request, const BackendContext& ctx) {
    Close();

    speed_ = clamp_speed(request.speed);
    audio_ = ctx.audio;

    // 按显示尺寸解码：源分辨率高于窗口时能省下数倍 CPU 转换与搬运（见 DecodeTarget.h）
    VideoSourceOptions opts;
    opts.maxWidth = ctx.width;
    opts.maxHeight = ctx.height;
    // 动图交给 FFmpeg：MF 只能解出首帧，动不起来
    opts.preferFfmpeg = (request.kind == BackendKind::AnimatedImage);

    std::string backend;
    source_ = open_video_source(request.sourcePath, &backend, opts);
    if (!source_) {
        wp_log("video backend: no decoder could open " + to_utf8(request.sourcePath));
        return false;
    }

    kind_ = (request.kind == BackendKind::AnimatedImage) ? BackendKind::AnimatedImage
                                                        : BackendKind::Video;
    name_ = backend.empty() ? "视频" : backend;

    // 音轨：只有真的存在才交给音频引擎；没有音轨时引擎保持静音
    if (audio_) {
        std::string audioBackend;
        auto audioSource = open_audio_source(request.sourcePath, &audioBackend);
        if (audioSource) {
            wp_log("video backend: audio via " + audioBackend);
        }
        audio_->SetSpeed(speed_);
        audio_->SetSource(std::move(audioSource));   // 空 = 无音轨，静音
    }

    lastTickUs_ = qpc_us();
    wp_log("video backend opened (" + name_ + "): " + to_utf8(request.sourcePath));
    return true;
}

void VideoBackend::Close() {
    if (audio_) {
        audio_->SetSource(nullptr);
        audio_ = nullptr;
    }
    if (source_) {
        source_->Close();
        source_.reset();
    }
    lastW_ = lastH_ = 0;
    advanceState_.Reset();
}

void VideoBackend::SetPaused(bool paused) {
    paused_ = paused;
    if (audio_) audio_->SetPaused(paused);
    if (!paused) lastTickUs_ = qpc_us();   // 恢复时重置计时，避免把暂停时长当成一次巨大间隔
}

double VideoBackend::TargetFps() const {
    // 源帧率（不乘速度：调速由 frame_advance_policy 按节拍消费帧实现）
    if (!source_) return 30.0;
    const double fps = source_->Fps();
    return fps > 1.0 ? fps : 30.0;
}

void VideoBackend::SetSpeed(double speed) {
    speed_ = clamp_speed(speed);
    if (audio_) audio_->SetSpeed(speed_);
}bool VideoBackend::ProduceFrame(std::vector<uint8_t>& bgra, int& w, int& h) {
    if (!source_) return false;
    if (paused_) return false;

    const int64_t now = qpc_us();
    const int64_t elapsedMs = (now - lastTickUs_) / 1000;
    lastTickUs_ = now;

    // 源帧时长：优先用源给出的逐帧延迟（GIF/WebP 可能变帧延迟），否则用 fps
    int64_t frameMs = source_->FrameDurationMs();
    if (frameMs <= 0) {
        const double fps = source_->Fps();
        frameMs = static_cast<int64_t>(1000.0 / (fps > 1.0 ? fps : 30.0));
    }

    const FrameAdvance adv = frame_advance_policy(advanceState_, speed_, elapsedMs, frameMs);

    if (adv.consume == 0) {
        // 不足以推进一帧：让调用方继续用缓冲区里的上一帧（零拷贝，不重取也不复制）
        if (lastW_ <= 0 || lastH_ <= 0) return false;   // 还没出过帧，无帧可保持
        w = lastW_;
        h = lastH_;
        return true;
    }

    // 一次拉 consume 帧：前 consume-1 帧是为调速追赶而丢弃的，最后一帧用于呈现
    bool got = false;
    for (int i = 0; i < adv.consume; ++i) {
        got = source_->NextFrame(bgra, w, h);
        if (!got) break;
    }
    if (!got) return false;

    lastW_ = w;
    lastH_ = h;
    ++serial_;
    return w > 0 && h > 0;
}

} // namespace desktopsticker::wallpaper
