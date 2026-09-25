#include "pch.h"
#include "VideoSource.h"

#include "FfmpegApi.h"
#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/DecodeTarget.h"

#include <atomic>

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

namespace {

std::atomic<int> g_mfRefs{0};

// ---------------- Media Foundation（主路径）----------------

class MfVideoSource final : public IVideoSource {
public:
    MfVideoSource(int maxW, int maxH) : maxW_(maxW), maxH_(maxH) {}
    ~MfVideoSource() override { Close(); }

    bool Open(const std::wstring& path) override {
        Close();

        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 1))) return false;
        // 允许 SourceReader 插入 Video Processor MFT，以便把解码输出转成 RGB32。
        //
        // 注意：这里**没有**配置硬件解码 —— 没有 MFCreateDXGIDeviceManager /
        // MF_SOURCE_READER_D3D_MANAGER，也没有 MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS。
        // 因此解码与 NV12→RGB32 转换都在 CPU 上跑，GPU 只负责我们自己的 D2D/DComp 上屏。
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader_))) {
            wp_log("MF: MFCreateSourceReaderFromURL failed for " + to_utf8(path));
            return false;
        }

        const DWORD videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader_->SetStreamSelection(videoStream, TRUE);

        // 源尺寸要从原生类型问：只有知道源多大才能算出等比的目标尺寸
        UINT32 srcW = 0, srcH = 0;
        {
            ComPtr<IMFMediaType> nativeType;
            if (SUCCEEDED(reader_->GetNativeMediaType(videoStream, 0, &nativeType))) {
                MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &srcW, &srcH);
            }
        }
        const ImageSize target = decode_target_size(static_cast<int>(srcW), static_cast<int>(srcH),
                                                   maxW_, maxH_);

        // 关键：把视频处理器当作"顺便缩放器"用。请求显示尺寸输出后，CPU 要转的
        // 像素数按屏幕走而不是按源走 —— 4K 源在 1080p 屏上是 4 倍差距。
        if (FAILED(set_output_type(videoStream, target))) {
            if (target.width != static_cast<int>(srcW) || target.height != static_cast<int>(srcH)) {
                wp_log("MF: scaled output rejected, decoding at source size");
            }
            if (FAILED(set_output_type(videoStream, {}))) {
                wp_log("MF: no usable decoder/video stream for " + to_utf8(path));
                Close();
                return false;
            }
        }

        ComPtr<IMFMediaType> current;
        if (FAILED(reader_->GetCurrentMediaType(videoStream, &current))) {
            Close();
            return false;
        }
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h);
        UINT32 num = 0, den = 1;
        MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &num, &den);
        width_ = static_cast<int>(w);
        height_ = static_cast<int>(h);
        fps_ = (den != 0 && num != 0) ? static_cast<double>(num) / den : 30.0;

        if (width_ <= 0 || height_ <= 0) {
            Close();
            return false;
        }
        wp_log("MF: opened " + to_utf8(path) + " " + std::to_string(width_) + "x" +
               std::to_string(height_) + " (source " + std::to_string(srcW) + "x" +
               std::to_string(srcH) + ")");
        return true;
    }

    void Close() override {
        reader_.Reset();
        width_ = height_ = 0;
    }

    bool NextFrame(std::vector<uint8_t>& bgra, int& width, int& height) override {
        if (!reader_) return false;

        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                               0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr)) {
            wp_log("MF: ReadSample failed hr=" + std::to_string(static_cast<long>(hr)));
            return false;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            SeekToStart();
            return false; // 回卷那一拍不出帧，调用方下轮重试
        }
        if (!sample) return false;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;

        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) return false;

        const size_t need = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u;
        const bool ok = curLen >= need;
        if (ok) {
            bgra.assign(data, data + need);
            width = width_;
            height = height_;
        }
        buffer->Unlock();
        return ok;
    }

    double Fps() const override { return fps_; }
    const char* Backend() const override { return "Media Foundation"; }

private:
    // size 为 {0,0} 表示不指定输出尺寸（按源分辨率解码）
    HRESULT set_output_type(DWORD videoStream, const ImageSize& size) {
        ComPtr<IMFMediaType> outType;
        if (FAILED(MFCreateMediaType(&outType))) return E_FAIL;
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (size.width > 0 && size.height > 0) {
            MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE,
                               static_cast<UINT32>(size.width), static_cast<UINT32>(size.height));
        }
        return reader_->SetCurrentMediaType(videoStream, nullptr, outType.Get());
    }

    void SeekToStart() {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        reader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
    }

    ComPtr<IMFSourceReader> reader_;
    int maxW_ = 0;
    int maxH_ = 0;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
};

#ifdef DSTK_HAVE_FFMPEG_SDK

// ---------------- FFmpeg 共享库（兜底路径）----------------

class FfmpegVideoSource final : public IVideoSource {
public:
    FfmpegVideoSource(int maxW, int maxH) : maxW_(maxW), maxH_(maxH) {}
    ~FfmpegVideoSource() override { Close(); }

    bool Open(const std::wstring& path) override {
        Close();
        FfmpegApi& api = FfmpegApi::Get();
        if (!api.Loaded()) return false;

        if (api.avformat_open_input(&format_, to_utf8(path).c_str(), nullptr, nullptr) < 0) {
            wp_log("FFmpeg: avformat_open_input failed for " + to_utf8(path));
            Close();
            return false;
        }
        if (api.avformat_find_stream_info(format_, nullptr) < 0) {
            Close();
            return false;
        }

        const AVCodec* codec = nullptr;
        const int index = api.av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (index < 0 || !codec) {
            wp_log("FFmpeg: no video stream");
            Close();
            return false;
        }
        streamIndex_ = index;

        codec_ = api.avcodec_alloc_context3(codec);
        if (!codec_) { Close(); return false; }
        if (api.avcodec_parameters_to_context(codec_, format_->streams[index]->codecpar) < 0) {
            Close();
            return false;
        }
        if (api.avcodec_open2(codec_, codec, nullptr) < 0) {
            wp_log("FFmpeg: avcodec_open2 failed");
            Close();
            return false;
        }

        packet_ = api.av_packet_alloc();
        frame_ = api.av_frame_alloc();
        if (!packet_ || !frame_) { Close(); return false; }

        AVRational rate = format_->streams[index]->avg_frame_rate;
        fps_ = (rate.num > 0 && rate.den > 0) ? static_cast<double>(rate.num) / rate.den : 30.0;
        width_ = codec_->width;
        height_ = codec_->height;
        if (width_ <= 0 || height_ <= 0) { Close(); return false; }

        wp_log("FFmpeg: opened " + to_utf8(path) + " " + std::to_string(width_) + "x" +
               std::to_string(height_));
        return true;
    }

    void Close() override {
        FfmpegApi& api = FfmpegApi::Get();
        if (sws_) { api.sws_freeContext(sws_); sws_ = nullptr; }
        if (frame_) { api.av_frame_free(&frame_); frame_ = nullptr; }
        if (packet_) { api.av_packet_free(&packet_); packet_ = nullptr; }
        if (codec_) { api.avcodec_free_context(&codec_); codec_ = nullptr; }
        if (format_) { api.avformat_close_input(&format_); format_ = nullptr; }
        streamIndex_ = -1;
        eof_ = false;
    }

    bool NextFrame(std::vector<uint8_t>& bgra, int& width, int& height) override {
        FfmpegApi& api = FfmpegApi::Get();
        if (!codec_ || !frame_) return false;

        for (int guard = 0; guard < 64; ++guard) {
            const int recv = api.avcodec_receive_frame(codec_, frame_);
            if (recv == 0) {
                const bool ok = convert(bgra, width, height);
                api.av_frame_unref(frame_);
                if (!ok) continue;
                return true;
            }
            if (recv == AVERROR(EAGAIN)) {
                if (!read_next_packet()) {
                    // 文件结束：回卷重播
                    api.av_seek_frame(format_, streamIndex_, 0, AVSEEK_FLAG_BACKWARD);
                    api.avcodec_flush_buffers(codec_);
                    eof_ = false;
                    return false;
                }
                continue;
            }
            return false; // 其它错误
        }
        return false;
    }

    double Fps() const override { return fps_; }
    // FFmpeg 给出逐帧时长（GIF 的变帧延迟靠它才正确）
    int FrameDurationMs() const override { return lastFrameDurationMs_; }
    const char* Backend() const override { return "FFmpeg"; }

private:
    bool read_next_packet() {
        FfmpegApi& api = FfmpegApi::Get();
        while (true) {
            const int r = api.av_read_frame(format_, packet_);
            if (r < 0) { eof_ = true; return false; }
            if (packet_->stream_index != streamIndex_) {
                api.av_packet_unref(packet_);
                continue;
            }
            const int send = api.avcodec_send_packet(codec_, packet_);
            api.av_packet_unref(packet_);
            if (send < 0) continue;
            return true;
        }
    }

    bool convert(std::vector<uint8_t>& bgra, int& width, int& height) {
        FfmpegApi& api = FfmpegApi::Get();
        const int w = frame_->width;
        const int h = frame_->height;
        if (w <= 0 || h <= 0) return false;

        // 缩放目标按当前帧尺寸算：流中分辨率变化时也能跟上
        const ImageSize target = decode_target_size(w, h, maxW_, maxH_);

        if (!sws_ || swsSrcW_ != w || swsSrcH_ != h || swsSrcFmt_ != frame_->format ||
            swsDstW_ != target.width || swsDstH_ != target.height) {
            if (sws_) api.sws_freeContext(sws_);
            sws_ = api.sws_getContext(w, h, static_cast<AVPixelFormat>(frame_->format),
                                      target.width, target.height, AV_PIX_FMT_BGRA, SWS_BILINEAR,
                                      nullptr, nullptr, nullptr);
            swsSrcW_ = w;
            swsSrcH_ = h;
            swsSrcFmt_ = frame_->format;
            swsDstW_ = target.width;
            swsDstH_ = target.height;
            if (!sws_) return false;
        }

        const int stride = target.width * 4;
        bgra.resize(static_cast<size_t>(stride) * static_cast<size_t>(target.height));
        uint8_t* dst[4] = { bgra.data(), nullptr, nullptr, nullptr };
        const int dstStride[4] = { stride, 0, 0, 0 };
        const int scaled = api.sws_scale(sws_, frame_->data, frame_->linesize, 0, h, dst, dstStride);
        if (scaled <= 0) return false;

        // 逐帧时长（GIF/WebP 的变帧延迟）：由帧时长 × 流时间基推出
        const AVRational tb = format_->streams[streamIndex_]->time_base;
        if (frame_->duration > 0 && tb.num > 0 && tb.den > 0) {
            lastFrameDurationMs_ = static_cast<int>(
                static_cast<int64_t>(frame_->duration) * 1000LL * tb.num / tb.den);
        }

        width = target.width;
        height = target.height;
        return true;
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    int swsSrcW_ = 0, swsSrcH_ = 0, swsSrcFmt_ = -1;
    int swsDstW_ = 0, swsDstH_ = 0;
    int maxW_ = 0;
    int maxH_ = 0;
    int streamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
    int lastFrameDurationMs_ = 0;
    bool eof_ = false;
};

#endif // DSTK_HAVE_FFMPEG_SDK

} // namespace

bool video_subsystem_start() {
    if (g_mfRefs.fetch_add(1) == 0) {
        const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            g_mfRefs.fetch_sub(1);
            wp_log("MFStartup failed hr=" + std::to_string(static_cast<long>(hr)));
            return false;
        }
    }
    return true;
}

void video_subsystem_stop() {
    if (g_mfRefs.fetch_sub(1) == 1) {
        MFShutdown();
    }
}

bool ffmpeg_fallback_available() {
    return FfmpegApi::Get().Loaded();
}

std::unique_ptr<IVideoSource> open_video_source(const std::wstring& path,
                                                std::string* chosenBackend,
                                                const VideoSourceOptions& options) {
    // 动图优先 FFmpeg：MF 只能给首帧，动图的逐帧延迟也只有 FFmpeg 路径报得出来
    auto try_ffmpeg = [&]() -> std::unique_ptr<IVideoSource> {
#ifdef DSTK_HAVE_FFMPEG_SDK
        auto ff = std::make_unique<FfmpegVideoSource>(options.maxWidth, options.maxHeight);
        if (ff->Open(path)) return ff;
#endif
        return nullptr;
    };

    if (options.preferFfmpeg) {
        if (auto ff = try_ffmpeg()) {
            if (chosenBackend) *chosenBackend = ff->Backend();
            return ff;
        }
        auto mf = std::make_unique<MfVideoSource>(options.maxWidth, options.maxHeight);
        if (mf->Open(path)) {
            wp_log("animated image: FFmpeg could not play it, using MF (first frame only)");
            if (chosenBackend) *chosenBackend = mf->Backend();
            return mf;
        }
        wp_log("no decoder could open the animated image: " + to_utf8(path));
        return nullptr;
    }

    auto mf = std::make_unique<MfVideoSource>(options.maxWidth, options.maxHeight);
    if (mf->Open(path)) {
        if (chosenBackend) *chosenBackend = mf->Backend();
        return mf;
    }

    // "系统缺少解码器时"的兜底：MF 打不开该素材才启用随包 FFmpeg
    if (auto ff = try_ffmpeg()) {
        wp_log("MF could not decode this media; using FFmpeg fallback");
        if (chosenBackend) *chosenBackend = ff->Backend();
        return ff;
    }
#ifndef DSTK_HAVE_FFMPEG_SDK
    wp_log("MF could not decode this media and no FFmpeg sdk was available at build time");
#endif

    wp_log("no decoder could open the media: " + to_utf8(path));
    return nullptr;
}

} // namespace desktopsticker::wallpaper
