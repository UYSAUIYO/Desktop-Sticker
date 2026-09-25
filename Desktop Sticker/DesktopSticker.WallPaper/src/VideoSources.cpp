#include "pch.h"
#include "VideoSource.h"

#include "FfmpegApi.h"
#include "Log.h"
#include "Utf8.h"

#include <atomic>

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

namespace {

std::atomic<int> g_mfRefs{0};

// ---------------- Media Foundation（主路径）----------------

class MfVideoSource final : public IVideoSource {
public:
    ~MfVideoSource() override { Close(); }

    bool Open(const std::wstring& path) override {
        Close();

        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 1))) return false;
        // 允许 Video Processor MFT 介入：硬解仍由 DXVA 完成，这里只是把输出转成 RGB32
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader_))) {
            wp_log("MF: MFCreateSourceReaderFromURL failed for " + to_utf8(path));
            return false;
        }

        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

        ComPtr<IMFMediaType> outType;
        if (FAILED(MFCreateMediaType(&outType))) return false;
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(reader_->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, outType.Get()))) {
            wp_log("MF: no usable decoder/video stream for " + to_utf8(path));
            Close();
            return false;
        }

        ComPtr<IMFMediaType> current;
        if (FAILED(reader_->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current))) {
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
               std::to_string(height_));
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
    void SeekToStart() {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        reader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
    }

    ComPtr<IMFSourceReader> reader_;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
};

#ifdef DSTK_HAVE_FFMPEG_SDK

// ---------------- FFmpeg 共享库（兜底路径）----------------

class FfmpegVideoSource final : public IVideoSource {
public:
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

        if (!sws_ || swsSrcW_ != w || swsSrcH_ != h || swsSrcFmt_ != frame_->format) {
            if (sws_) api.sws_freeContext(sws_);
            sws_ = api.sws_getContext(w, h, static_cast<AVPixelFormat>(frame_->format),
                                      w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR,
                                      nullptr, nullptr, nullptr);
            swsSrcW_ = w;
            swsSrcH_ = h;
            swsSrcFmt_ = frame_->format;
            if (!sws_) return false;
        }

        const int stride = w * 4;
        bgra.resize(static_cast<size_t>(stride) * static_cast<size_t>(h));
        uint8_t* dst[4] = { bgra.data(), nullptr, nullptr, nullptr };
        const int dstStride[4] = { stride, 0, 0, 0 };
        const int scaled = api.sws_scale(sws_, frame_->data, frame_->linesize, 0, h, dst, dstStride);
        if (scaled <= 0) return false;

        width = w;
        height = h;
        return true;
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    int swsSrcW_ = 0, swsSrcH_ = 0, swsSrcFmt_ = -1;
    int streamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
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
                                                std::string* chosenBackend) {
    auto mf = std::make_unique<MfVideoSource>();
    if (mf->Open(path)) {
        if (chosenBackend) *chosenBackend = mf->Backend();
        return mf;
    }

#ifdef DSTK_HAVE_FFMPEG_SDK
    // "系统缺少解码器时"的兜底：MF 打不开该素材才启用随包 FFmpeg
    auto ff = std::make_unique<FfmpegVideoSource>();
    if (ff->Open(path)) {
        wp_log("MF could not decode this media; using FFmpeg fallback");
        if (chosenBackend) *chosenBackend = ff->Backend();
        return ff;
    }
#else
    wp_log("MF could not decode this media and no FFmpeg sdk was available at build time");
#endif

    wp_log("no decoder could open the media: " + to_utf8(path));
    return nullptr;
}

} // namespace desktopsticker::wallpaper
