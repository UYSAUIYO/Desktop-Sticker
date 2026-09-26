#include "pch.h"
#include "VideoBackend.h"

#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/DecodePath.h"
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

    // 解码/渲染路径由用户选（设置页可随时改）。
    // 顺序按"硬件优先"：FFmpeg+NVDEC 最快 → MF/D3D11 → 纯软件。
    // 每一档失败都会打日志并向下退让，不会黑屏、也不会打不开素材。
    decodePath_ = request.decodePath;
    switch (decodePath_) {
        case DecodePath::Auto:
            opts.useNvdec = true;                 // 先试 FFmpeg 硬解
            if (ctx.nv12Present) opts.d3dDevice = ctx.d3dDevice;   // 退到 MF 时还能走 D3D11 硬解
            break;
        case DecodePath::FfmpegHardware:
            opts.useNvdec = true;
            break;
        case DecodePath::MediaFoundationD3d:
            if (ctx.nv12Present) opts.d3dDevice = ctx.d3dDevice;
            else wp_log("decode path: MF/D3D11 requested but NV12 present is unavailable");
            break;
        case DecodePath::Cpu:
            break;                                // 什么都不开 = 纯软件
    }

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
    wp_log(std::string("video backend opened (") + name_ + ", path=" +
           decode_path_to_string(request.decodePath) + "): " + to_utf8(request.sourcePath));
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
}bool VideoBackend::ProduceFrame(VideoFrame& out) {
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

    // 追赶上限：拍与拍之间欠的帧**不能全量补**——补一帧就要解一帧，欠得越多单拍越重，
    // 拍长随之拉长、欠账进一步变多，最后锁死在"解了一堆帧、几乎不呈现"的慢速固定点
    // （实测 4K60：解码 92 帧/s，呈现只有 ~19 帧/s）。超出的欠账交给渲染循环的
    // deadline 重锚丢弃（那才是跳帧的正确位置：不花解码的钱）。
    //
    // 上限分两种主时钟取值（见 ClockPolicy.h）：
    //   · 静音（QPC 主时钟）：ceil(speed)——1 倍速=每拍 1 帧=每拍 1 次呈现，
    //     呈现帧率不再被"补 2 呈现 1"腰斩；
    //   · 有声（音频主时钟）：再加 1——音频时钟按 ~10ms 混合周期量化，节拍天生偏长，
    //     每拍只解 1 帧会让内容慢于声音、持续漂移；多解出的帧里只有最后一帧上屏，
    //     用呈现帧率换音画同步。
    int consume = adv.consume;
    int maxConsume = std::max(1, static_cast<int>(std::ceil(speed_)));
    if (audio_ && !audio_->Muted()) ++maxConsume;
    if (consume > maxConsume) consume = maxConsume;

    // 不足以推进一帧：报"无新帧"。调用方不重新呈现，DComp 保住上一帧 ——
    // 比把同一帧再上传一遍省掉一次整帧拷贝 + 一次 Present。
    if (consume <= 0) return false;

    // 一次拉 consume 帧：前 consume-1 帧是为调速追赶而丢弃的，最后一帧用于呈现
    bool got = false;
    for (int i = 0; i < consume; ++i) {
        got = source_->NextFrame(out);
        if (!got) break;
    }
    if (!got) return false;

    ++serial_;
    return out.width > 0 && out.height > 0;
}

} // namespace desktopsticker::wallpaper
