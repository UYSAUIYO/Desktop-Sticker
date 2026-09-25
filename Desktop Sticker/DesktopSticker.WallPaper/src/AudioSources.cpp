#include "pch.h"
#include "AudioSource.h"

#include "FfmpegApi.h"
#include "Log.h"
#include "Utf8.h"

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

namespace {

// ---------------- Media Foundation（主路径）----------------
// 与画面一致：MF 能解就用 MF，只有 MF 报告无法解码时才落到 FFmpeg。

class MfAudioSource final : public IAudioSource {
public:
    ~MfAudioSource() override { Close(); }

    bool Open(const std::wstring& path) {
        Close();

        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 1))) return false;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader_))) {
            return false;   // 无音轨 / 打不开：静默降级，不是错误
        }

        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        if (FAILED(reader_->SetStreamSelection(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE))) {
            Close();
            return false;
        }

        ComPtr<IMFMediaType> outType;
        if (FAILED(MFCreateMediaType(&outType))) { Close(); return false; }
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);   // 16-bit PCM
        if (FAILED(reader_->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
                outType.Get()))) {
            Close();
            return false;
        }

        ComPtr<IMFMediaType> current;
        if (FAILED(reader_->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &current))) {
            Close();
            return false;
        }
        UINT32 ch = 0, rate = 0;
        current->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        current->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        if (ch == 0 || rate == 0) { Close(); return false; }

        channels_ = static_cast<int>(ch);
        rate_ = static_cast<int>(rate);
        return true;
    }

    void Close() override {
        reader_.Reset();
        leftover_.clear();
        leftoverOffset_ = 0;
        channels_ = 0;
        rate_ = 0;
    }

    bool HasAudio() const override { return reader_ != nullptr && channels_ > 0; }

    size_t ReadFrames(int16_t* out, size_t maxFrames) override {
        if (!reader_ || !out || maxFrames == 0 || channels_ <= 0) return 0;
        const size_t frameBytes = static_cast<size_t>(channels_) * sizeof(int16_t);
        size_t produced = 0;

        // 先吐出上一批没消费完的部分（一次 ReadSample 可能带多帧）
        while (produced < maxFrames && leftoverOffset_ < leftover_.size()) {
            const size_t take = std::min(maxFrames - produced,
                                         (leftover_.size() - leftoverOffset_) / frameBytes);
            if (take == 0) break;
            std::memcpy(out + produced * static_cast<size_t>(channels_),
                        leftover_.data() + leftoverOffset_, take * frameBytes);
            leftoverOffset_ += take * frameBytes;
            produced += take;
        }
        if (leftoverOffset_ >= leftover_.size()) {
            leftover_.clear();
            leftoverOffset_ = 0;
        }
        if (produced >= maxFrames) return produced;

        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, &ts,
            &sample);
        if (FAILED(hr)) return produced;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return produced; // 0 → 调用方回卷
        if (!sample) return produced;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return produced;

        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) return produced;

        const size_t availFrames = curLen / frameBytes;
        const size_t want = maxFrames - produced;
        const size_t take = std::min(availFrames, want);
        std::memcpy(out + produced * static_cast<size_t>(channels_), data, take * frameBytes);
        produced += take;

        if (take < availFrames) {
            leftover_.assign(data + take * frameBytes, data + curLen);
            leftoverOffset_ = 0;
        }
        buffer->Unlock();
        return produced;
    }

    int Channels() const override { return channels_; }
    int SampleRate() const override { return rate_; }

    bool SeekToStart() override {
        if (!reader_) return false;
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        leftover_.clear();
        leftoverOffset_ = 0;
        return SUCCEEDED(hr);
    }

    const char* Backend() const override { return "Media Foundation"; }

private:
    ComPtr<IMFSourceReader> reader_;
    std::vector<uint8_t> leftover_;
    size_t leftoverOffset_ = 0;
    int channels_ = 0;
    int rate_ = 0;
};

#ifdef DSTK_HAVE_FFMPEG_SDK

// ---------------- FFmpeg 共享库（兜底路径）----------------

class FfmpegAudioSource final : public IAudioSource {
public:
    ~FfmpegAudioSource() override { Close(); }

    bool Open(const std::wstring& path) {
        Close();
        FfmpegApi& api = FfmpegApi::Get();
        if (!api.Loaded()) return false;

        if (api.avformat_open_input(&format_, to_utf8(path).c_str(), nullptr, nullptr) < 0) {
            Close();
            return false;
        }
        if (api.avformat_find_stream_info(format_, nullptr) < 0) { Close(); return false; }

        const AVCodec* codec = nullptr;
        const int index = api.av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (index < 0 || !codec) { Close(); return false; }   // 无音轨
        streamIndex_ = index;

        codec_ = api.avcodec_alloc_context3(codec);
        if (!codec_) { Close(); return false; }
        if (api.avcodec_parameters_to_context(codec_, format_->streams[index]->codecpar) < 0) {
            Close();
            return false;
        }
        if (api.avcodec_open2(codec_, codec, nullptr) < 0) { Close(); return false; }

        packet_ = api.av_packet_alloc();
        frame_ = api.av_frame_alloc();
        if (!packet_ || !frame_) { Close(); return false; }

        // 统一转成 S16 交错、保持源采样率与声道数。
        // 注意 swr_alloc_set_opts2 的返回值是 int，上下文从第一个参数（SwrContext**）取出。
        if (api.swr_alloc_set_opts2(&swr_,
                                    &codec_->ch_layout, AV_SAMPLE_FMT_S16, codec_->sample_rate,
                                    &codec_->ch_layout, codec_->sample_fmt, codec_->sample_rate,
                                    0, nullptr) < 0 ||
            !swr_) {
            Close();
            return false;
        }
        if (api.swr_init(swr_) < 0) { Close(); return false; }

        channels_ = codec_->ch_layout.nb_channels;
        rate_ = codec_->sample_rate;
        if (channels_ <= 0 || rate_ <= 0) { Close(); return false; }
        return true;
    }

    void Close() override {
        FfmpegApi& api = FfmpegApi::Get();
        if (swr_) { api.swr_free(&swr_); swr_ = nullptr; }
        if (frame_) { api.av_frame_free(&frame_); frame_ = nullptr; }
        if (packet_) { api.av_packet_free(&packet_); packet_ = nullptr; }
        if (codec_) { api.avcodec_free_context(&codec_); codec_ = nullptr; }
        if (format_) { api.avformat_close_input(&format_); format_ = nullptr; }
        streamIndex_ = -1;
        converted_.clear();
        convertedOffset_ = 0;
        channels_ = 0;
        rate_ = 0;
    }

    bool HasAudio() const override { return codec_ != nullptr && channels_ > 0; }

    size_t ReadFrames(int16_t* out, size_t maxFrames) override {
        if (!codec_ || !out || maxFrames == 0 || channels_ <= 0) return 0;
        const size_t frameBytes = static_cast<size_t>(channels_) * sizeof(int16_t);
        size_t produced = 0;

        while (produced < maxFrames) {
            // 先消费已转换的存货
            const size_t availFrames =
                (converted_.size() - convertedOffset_) / frameBytes;
            if (availFrames > 0) {
                const size_t take = std::min(maxFrames - produced, availFrames);
                std::memcpy(out + produced * static_cast<size_t>(channels_),
                            converted_.data() + convertedOffset_, take * frameBytes);
                convertedOffset_ += take * frameBytes;
                produced += take;
                if (convertedOffset_ >= converted_.size()) {
                    converted_.clear();
                    convertedOffset_ = 0;
                }
                continue;
            }

            FfmpegApi& api = FfmpegApi::Get();
            const int recv = api.avcodec_receive_frame(codec_, frame_);
            if (recv == 0) {
                if (!convert_frame()) { api.av_frame_unref(frame_); return produced; }
                api.av_frame_unref(frame_);
                continue;
            }
            if (recv != AVERROR(EAGAIN)) return produced;   // 解码结束或出错

            // 需要更多 packet
            bool sent = false;
            while (!sent) {
                const int r = api.av_read_frame(format_, packet_);
                if (r < 0) return produced;                 // 文件结束 → 调用方回卷
                if (packet_->stream_index != streamIndex_) {
                    api.av_packet_unref(packet_);
                    continue;
                }
                const int s = api.avcodec_send_packet(codec_, packet_);
                api.av_packet_unref(packet_);
                if (s < 0) continue;
                sent = true;
            }
        }
        return produced;
    }

    int Channels() const override { return channels_; }
    int SampleRate() const override { return rate_; }

    bool SeekToStart() override {
        if (!format_ || streamIndex_ < 0) return false;
        FfmpegApi& api = FfmpegApi::Get();
        api.av_seek_frame(format_, streamIndex_, 0, AVSEEK_FLAG_BACKWARD);
        api.avcodec_flush_buffers(codec_);
        converted_.clear();
        convertedOffset_ = 0;
        return true;
    }

    const char* Backend() const override { return "FFmpeg"; }

private:
    bool convert_frame() {
        FfmpegApi& api = FfmpegApi::Get();
        const int frames = frame_->nb_samples;
        if (frames <= 0) return false;

        // 输出上限：给足余量，S16 交错
        const size_t cap = static_cast<size_t>(frames) * static_cast<size_t>(channels_) *
                           sizeof(int16_t);
        converted_.resize(cap);
        uint8_t* outPlanes[1] = { converted_.data() };
        const int got = api.swr_convert(swr_, outPlanes, frames,
                                        reinterpret_cast<const uint8_t* const*>(frame_->extended_data),
                                        frames);
        if (got <= 0) {
            converted_.clear();
            return false;
        }
        converted_.resize(static_cast<size_t>(got) * static_cast<size_t>(channels_) *
                          sizeof(int16_t));
        convertedOffset_ = 0;
        return true;
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwrContext* swr_ = nullptr;
    int streamIndex_ = -1;
    int channels_ = 0;
    int rate_ = 0;
    std::vector<uint8_t> converted_;
    size_t convertedOffset_ = 0;
};

#endif // DSTK_HAVE_FFMPEG_SDK

} // namespace

std::unique_ptr<IAudioSource> open_audio_source(const std::wstring& path,
                                                std::string* chosenBackend) {
    auto mf = std::make_unique<MfAudioSource>();
    if (mf->Open(path)) {
        if (mf->HasAudio()) {
            if (chosenBackend) *chosenBackend = mf->Backend();
            return mf;
        }
        return nullptr;   // 文件本身没有音轨：不是错误，静默返回
    }

#ifdef DSTK_HAVE_FFMPEG_SDK
    auto ff = std::make_unique<FfmpegAudioSource>();
    if (ff->Open(path) && ff->HasAudio()) {
        if (chosenBackend) *chosenBackend = ff->Backend();
        return ff;
    }
#endif

    return nullptr;
}

} // namespace desktopsticker::wallpaper
