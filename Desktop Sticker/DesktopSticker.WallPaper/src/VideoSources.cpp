#include "pch.h"
#include "VideoSource.h"

#include "FfmpegApi.h"
#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/DecodeTarget.h"

#include <atomic>
#include <mfidl.h>

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

namespace {

std::atomic<int> g_mfRefs{0};

// D3D11 设备管理器是**按设备**的，而本项目全程只有一个渲染线程、一个 D3D 设备，
// 所以进程内缓存一份即可；设备换了（理论上不会）就重建。
ComPtr<IMFDXGIDeviceManager>& shared_d3d_manager(ID3D11Device* device, UINT& resetToken,
                                                bool& ok) {
    static ComPtr<IMFDXGIDeviceManager> manager;
    static ID3D11Device* bound = nullptr;
    ok = false;
    if (device != bound) {
        manager.Reset();
        bound = nullptr;
    }
    if (!manager) {
        if (FAILED(MFCreateDXGIDeviceManager(&resetToken, &manager)) || !manager) {
            wp_log("MF: MFCreateDXGIDeviceManager failed; hardware decode unavailable");
            return manager;
        }
    }
    if (bound != device) {
        if (FAILED(manager->ResetDevice(device, resetToken))) {
            wp_log("MF: IMFDXGIDeviceManager::ResetDevice failed; hardware decode unavailable");
            manager.Reset();
            return manager;
        }
        bound = device;
    }
    ok = true;
    return manager;
}

// ---------------- Media Foundation ----------------

class MfVideoSource final : public IVideoSource {
public:
    MfVideoSource(int maxW, int maxH, ID3D11Device* device)
        : maxW_(maxW), maxH_(maxH), device_(device) {}
    ~MfVideoSource() override { Close(); }

    bool Open(const std::wstring& path) override {
        Close();

        // 先试硬解（NV12 纹理），失败再退回沿用已久的软件 RGB32 路径。
        // 两条路的取舍只在"帧从哪来"，上层的节奏/调速逻辑完全一致。
        if (device_ && open_reader(path, /*useD3d=*/true) && probe_dxgi_frame()) {
            gpu_ = true;
            wp_log("MF: opened (D3D11 hardware decode) " + to_utf8(path) + " " +
                   std::to_string(width_) + "x" + std::to_string(height_) + " (source " +
                   std::to_string(sourceW_) + "x" + std::to_string(sourceH_) + ")");
            return true;
        }
        Close();
        if (!open_reader(path, /*useD3d=*/false)) return false;
        gpu_ = false;
        wp_log("MF: opened " + to_utf8(path) + " " + std::to_string(width_) + "x" +
               std::to_string(height_) + " (source " + std::to_string(sourceW_) + "x" +
               std::to_string(sourceH_) + ")");
        return true;
    }

    void Close() override {
        pendingTexture_.Reset();
        lastSample_.Reset();
        reader_.Reset();
        manager_.Reset();
        gpu_ = false;
        width_ = height_ = sourceW_ = sourceH_ = 0;
        pendingGpu_ = false;
        pendingPts_ = -1;
    }

    bool NextFrame(VideoFrame& out) override {
        if (!reader_) return false;

        // 探测时已经读出的一帧，先交付。样本仍由 lastSample_ 持有，所以这里必须把自己那份
        // 纹理引用放掉 —— 多持一份会让解码器的输出池耗尽，下一次 ReadSample 直接**永久阻塞**
        // （实测就是这样把整个渲染线程连同退出流程一起挂死的）。
        if (pendingGpu_ && pendingTexture_) {
            out.texture = pendingTexture_.Get();
            out.subresource = pendingSubresource_;
            out.width = width_;
            out.height = height_;
            out.pts100ns = pendingPts_;
            pendingGpu_ = false;
            pendingTexture_.Reset();
            return true;
        }

        // 先把上一帧还回解码器的输出池，再要下一帧：否则我们占着纹理，解码器可能没有输出可用
        lastSample_.Reset();

        static std::atomic<int> s_probe{0};
        const bool trace = gpu_ && s_probe.fetch_add(1) < 24;
        if (trace) wp_log("mf/gpu: ReadSample enter");

        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                               0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr)) {
            wp_log("MF: ReadSample failed hr=" + std::to_string(static_cast<long>(hr)));
            return false;
        }
        if (trace) wp_log("mf/gpu: ReadSample done flags=" + std::to_string(flags) +
                          " sample=" + (sample ? "1" : "0"));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            SeekToStart();
            return false;   // 回卷那一拍不出帧，调用方下轮重试
        }
        if (!sample) return false;

        if (gpu_) {
            const bool ok = deliver_gpu(sample.Get(), out);
            if (ok) out.pts100ns = timestamp;
            if (ok && trace) {
                // 把"每次交付的到底是哪块显存"打出来：纹理或切片不再变化 = 画面冻住；
                // pts 重复 = 解码器在给我们重复帧；两者都推进 = 问题在呈现侧。
                char buf[160]{};
                snprintf(buf, sizeof(buf), "mf/gpu: delivered tex=%p sub=%u pts=%lldms",
                         reinterpret_cast<void*>(out.texture), out.subresource,
                         static_cast<long long>(timestamp / 10000));
                wp_log(buf);
            }
            return ok;
        }

        // CPU 路径：一次拷贝进调用方缓冲
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) return false;
        if (!out.pixels) { buffer->Unlock(); return false; }

        const size_t need = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u;
        const bool ok = curLen >= need;
        if (ok) {
            out.pixels->assign(data, data + need);
            out.width = width_;
            out.height = height_;
        }
        buffer->Unlock();
        return ok;
    }

    double Fps() const override { return fps_; }
    const char* Backend() const override {
        return gpu_ ? "Media Foundation (D3D11 硬解)" : "Media Foundation";
    }
    bool UsesGpu() const override { return gpu_; }

private:
    // ---- 打开设备上的 SourceReader ----
    bool open_reader(const std::wstring& path, bool useD3d) {
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 4))) return false;

        if (useD3d) {
            UINT resetToken = 0;
            bool managerOk = false;
            manager_ = shared_d3d_manager(device_, resetToken, managerOk);
            if (!managerOk || !manager_) return false;
            if (FAILED(attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager_.Get()))) {
                return false;
            }
            // 允许 SourceReader 插入硬件 MFT（硬解的必要条件之一）
            attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        }
        // 允许插入视频处理器：CPU 路径靠它做 NV12→RGB32 与缩放；
        // 硬解路径靠它把输出尺寸改成显示尺寸（改不动就按源尺寸，由着色器缩放兜住）
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader_))) {
            if (useD3d) return false;   // 硬解这条失败就走软件，不必刷屏
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
        sourceW_ = static_cast<int>(srcW);
        sourceH_ = static_cast<int>(srcH);

        const ImageSize target = decode_target_size(sourceW_, sourceH_, maxW_, maxH_);
        const GUID wantFormat = useD3d ? MFVideoFormat_NV12 : MFVideoFormat_RGB32;

        // 硬解先按显示尺寸请求（省下着色器的缩放），被拒就退回源尺寸（缩放由着色器的 UV 变换兜住）
        bool typeOk = set_output_type(videoStream, wantFormat, target);
        if (!typeOk && useD3d) typeOk = set_output_type(videoStream, wantFormat, {});
        if (!typeOk) return false;

        ComPtr<IMFMediaType> current;
        if (FAILED(reader_->GetCurrentMediaType(videoStream, &current))) return false;
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h);
        UINT32 num = 0, den = 1;
        MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &num, &den);
        width_ = static_cast<int>(w);
        height_ = static_cast<int>(h);
        fps_ = (den != 0 && num != 0) ? static_cast<double>(num) / den : 30.0;
        return width_ > 0 && height_ > 0;
    }

    bool set_output_type(DWORD videoStream, const GUID& format, const ImageSize& size) {
        ComPtr<IMFMediaType> outType;
        if (FAILED(MFCreateMediaType(&outType))) return false;
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, format);
        if (size.width > 0 && size.height > 0) {
            MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE,
                               static_cast<UINT32>(size.width), static_cast<UINT32>(size.height));
        }
        return SUCCEEDED(reader_->SetCurrentMediaType(videoStream, nullptr, outType.Get()));
    }

    // 硬解第一帧不一定真给 DXGI 缓冲（有些解码器最后一帧走内存），所以开完流先探一帧。
    // 探到就把这一帧留着交付，避免白丢一帧。
    bool probe_dxgi_frame() {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
                                       nullptr, &flags, &timestamp, &sample))) {
            return false;
        }
        if (!sample || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) return false;

        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFDXGIBuffer> dxgi;
        if (FAILED(sample->GetBufferByIndex(0, &buffer)) || !buffer ||
            FAILED(buffer.As(&dxgi)) || !dxgi) {
            wp_log("MF: NV12 sample is not a DXGI buffer; falling back to software decode");
            return false;
        }
        ComPtr<ID3D11Texture2D> texture;
        UINT subresource = 0;
        if (FAILED(dxgi->GetResource(IID_PPV_ARGS(&texture))) || !texture ||
            FAILED(dxgi->GetSubresourceIndex(&subresource))) {
            wp_log("MF: DXGI sample has no ID3D11Texture2D; falling back to software decode");
            return false;
        }
        // 解码器输出的真实形状（尺寸/数组/绑定标志）决定呈现侧怎么拷，一次性写进日志
        D3D11_TEXTURE2D_DESC td{};
        texture->GetDesc(&td);
        wp_log("MF: decoder output texture " + std::to_string(td.Width) + "x" +
               std::to_string(td.Height) + " array=" + std::to_string(td.ArraySize) +
               " format=" + std::to_string(static_cast<int>(td.Format)) +
               " bind=" + std::to_string(td.BindFlags) + " subresource=" +
               std::to_string(subresource));
        lastSample_ = sample;            // 持有样本 = 持有纹理，保证探测帧可交付
        pendingTexture_ = texture;
        pendingSubresource_ = subresource;
        pendingPts_ = timestamp;
        pendingGpu_ = true;
        return true;
    }

    bool deliver_gpu(IMFSample* sample, VideoFrame& out) {
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFDXGIBuffer> dxgi;
        if (FAILED(sample->GetBufferByIndex(0, &buffer)) || !buffer ||
            FAILED(buffer.As(&dxgi)) || !dxgi) {
            return false;
        }
        ComPtr<ID3D11Texture2D> texture;
        UINT subresource = 0;
        if (FAILED(dxgi->GetResource(IID_PPV_ARGS(&texture))) || !texture ||
            FAILED(dxgi->GetSubresourceIndex(&subresource))) {
            return false;
        }
        // 纹理由样本持有，样本由我们持有到下一次 NextFrame —— 在此之前纹理有效
        lastSample_ = sample;
        out.texture = texture.Get();
        out.subresource = subresource;
        out.width = width_;
        out.height = height_;
        return true;
    }

    void SeekToStart() {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        reader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        // 回卷后解码器可能换用别的输出纹理，手上的帧作废
        lastSample_.Reset();
        pendingTexture_.Reset();
        pendingGpu_ = false;
    }

    ComPtr<IMFSourceReader> reader_;
    ComPtr<IMFDXGIDeviceManager> manager_;
    ID3D11Device* device_ = nullptr;
    // 硬解帧必须"活到下一次 NextFrame"：靠持有样本把纹理钉住（同时只持有一个，见 NextFrame 开头）
    ComPtr<IMFSample> lastSample_;
    ComPtr<ID3D11Texture2D> pendingTexture_;
    // 探测帧的媒体时间戳，随 pendingTexture_ 一起留给第一次交付
    int64_t pendingPts_ = -1;
    uint32_t pendingSubresource_ = 0;
    bool pendingGpu_ = false;
    bool gpu_ = false;
    int maxW_ = 0;
    int maxH_ = 0;
    int sourceW_ = 0;
    int sourceH_ = 0;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
};

#ifdef DSTK_HAVE_FFMPEG_SDK

// ---------------- FFmpeg 共享库（NVDEC 硬解 + CPU 软解兜底）----------------

// 等价于命令行的 `-hwaccel cuda`：解码器给出的候选格式里只要有 CUDA 就选它。
// **这个回调缺了就没有硬解输出** —— 只挂 `hw_device_ctx` 时 `ff_get_format` 会挑软件格式，
// 帧落在系统内存里，缩放与色彩转换全压在 CPU 上（实测按名字开 `h264_cuvid` 也一样，
// 它自己把 4K 帧下载回来，日志里的 `Formats: HW: nv12 | SW: nv12` 就是这件事）。
AVPixelFormat pick_hw_format(AVCodecContext* ctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; p && *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_CUDA) return *p;
    }
    return fmts[0];   // 没有 CUDA 候选就用解码器首选的软件格式（兜底仍然能出图）
}

// FFmpeg 的错误该以文本进日志：`-22` 这种数字在日志里没人能反查，而这一轮的账正好
// 压在"链配不起来却一句话都不说"上。
std::string ff_err(int code) {
    FfmpegApi& api = FfmpegApi::Get();
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    if (api.Loaded() && api.av_strerror(code, buf, sizeof(buf)) == 0) return buf;
    return "code " + std::to_string(code);
}

class FfmpegVideoSource final : public IVideoSource {
public:
    FfmpegVideoSource(int maxW, int maxH, bool useNvdec)
        : maxW_(maxW), maxH_(maxH), useNvdec_(useNvdec) {}
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

        // 硬解：建 CUDA 设备，让通用解码器在它上面解（等价 `-hwaccel cuda`）。
        // 任一环节不成就用软件解码器重来 —— "这条路走不通才降级"落在下面那两次 open_decoder 里。
        if (useNvdec_ && api.av_hwdevice_ctx_create) {
            AVBufferRef* device = nullptr;
            if (api.av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0) >= 0 &&
                device) {
                hwDevice_ = device;   // 所有权留在我们这儿，Close 时一起放
            } else {
                wp_log("FFmpeg: CUDA device unavailable; NVDEC disabled for this item");
            }
        }

        // 建上下文 + 挂设备 + 装格式回调 + 打开，一起做；不成再不带硬件重来一次
        auto open_decoder = [&](bool withHw) -> bool {
            codec_ = api.avcodec_alloc_context3(codec);
            if (!codec_) return false;
            if (api.avcodec_parameters_to_context(codec_, format_->streams[index]->codecpar) < 0) {
                api.avcodec_free_context(&codec_);
                codec_ = nullptr;
                return false;
            }
            if (withHw) {
                codec_->hw_device_ctx = api.av_buffer_ref(hwDevice_);
                if (!codec_->hw_device_ctx) {
                    api.avcodec_free_context(&codec_);
                    codec_ = nullptr;
                    return false;
                }
                codec_->get_format = pick_hw_format;
            }
            if (api.avcodec_open2(codec_, codec, nullptr) < 0) {
                api.avcodec_free_context(&codec_);
                codec_ = nullptr;
                return false;
            }
            return true;
        };

        hwDecoder_ = hwDevice_ && open_decoder(true);
        if (!hwDecoder_) {
            if (hwDevice_) wp_log("FFmpeg: hardware decode context failed; decoding on CPU");
            if (!open_decoder(false)) {
                wp_log("FFmpeg: avcodec_open2 failed");
                Close();
                return false;
            }
        }

        packet_ = api.av_packet_alloc();
        frame_ = api.av_frame_alloc();
        if (!packet_ || !frame_) { Close(); return false; }

        AVRational rate = format_->streams[index]->avg_frame_rate;
        fps_ = (rate.num > 0 && rate.den > 0) ? static_cast<double>(rate.num) / rate.den : 30.0;
        width_ = codec_->width;
        height_ = codec_->height;
        if (width_ <= 0 || height_ <= 0) { Close(); return false; }

        // 硬解帧走 GPU 侧缩放链（需要 avfilter 里的 scale_cuda）。没有滤镜链时退化成
        // "整帧 4K NV12 搬回内存再 swscale"，仍然可用，只是 CPU 更忙。
        useCudaChain_ = hwDecoder_ && api.FilterOk();

        wp_log(std::string("FFmpeg: opened ") + (hwDecoder_ ? "[NVDEC] " : "") + to_utf8(path) +
               " " + std::to_string(width_) + "x" + std::to_string(height_) +
               (hwDecoder_ ? (useCudaChain_ ? " (GPU 缩放链已启用)" : " (无滤镜链，整帧回读)")
                           : ""));
        return true;
    }

    void Close() override {
        FfmpegApi& api = FfmpegApi::Get();
        if (graph_) { api.avfilter_graph_free(&graph_); graph_ = nullptr; }
        graphSrc_ = nullptr;
        graphSink_ = nullptr;
        if (filterOut_) { api.av_frame_free(&filterOut_); filterOut_ = nullptr; }
        if (swFrame_) { api.av_frame_free(&swFrame_); swFrame_ = nullptr; }
        if (sws_) { api.sws_freeContext(sws_); sws_ = nullptr; }
        if (frame_) { api.av_frame_free(&frame_); frame_ = nullptr; }
        if (packet_) { api.av_packet_free(&packet_); packet_ = nullptr; }
        if (codec_) {
            if (codec_->hw_device_ctx) api.av_buffer_unref(&codec_->hw_device_ctx);
            api.avcodec_free_context(&codec_);
            codec_ = nullptr;
        }
        if (hwDevice_) { api.av_buffer_unref(&hwDevice_); hwDevice_ = nullptr; }
        if (format_) { api.avformat_close_input(&format_); format_ = nullptr; }
        streamIndex_ = -1;
        eof_ = false;
        hwDecoder_ = false;
        useCudaChain_ = false;
        chainRebuildTried_ = false;
        firstFormatLogged_ = false;
        guardWarned_ = false;
    }

    bool NextFrame(VideoFrame& out) override {
        FfmpegApi& api = FfmpegApi::Get();
        if (!codec_ || !frame_ || !out.pixels) return false;

        for (int guard = 0; guard < 64; ++guard) {
            const int recv = api.avcodec_receive_frame(codec_, frame_);
            if (recv == 0) {
                const bool ok = convert(*out.pixels, out.width, out.height);
                api.av_frame_unref(frame_);
                if (!ok) continue;
                return true;
            }
            if (recv == AVERROR(EAGAIN)) {
                if (!read_next_packet()) {
                    // 文件结束：回卷重播。NVDEC 回卷后可能派生**新的**输出帧池，
                    // 而滤镜图里钉着旧池 —— 不重建的话 add_frame 每帧都失败，
                    // 整条 GPU 链会无声地退回"整帧 4K 回读"（实测就是这样掉到 7fps 的）。
                    api.av_seek_frame(format_, streamIndex_, 0, AVSEEK_FLAG_BACKWARD);
                    api.avcodec_flush_buffers(codec_);
                    drop_cuda_graph();
                    eof_ = false;
                    return false;
                }
                continue;
            }
            return false; // 其它错误
        }
        // 走到这里说明连续 64 帧都解出来了却一帧都没转换成功 —— 转换链坏了。
        // 不写出来的话，现象只是"帧率很低"，看不出原因（这次就是）。
        if (!guardWarned_) {
            guardWarned_ = true;
            wp_log("FFmpeg: frame guard exhausted (64 decoded frames, none converted)");
        }
        return false;
    }

    double Fps() const override { return fps_; }
    // FFmpeg 给出逐帧时长（GIF 的变帧延迟靠它才正确）
    int FrameDurationMs() const override { return lastFrameDurationMs_; }
    const char* Backend() const override {
        return hwDecoder_ ? "FFmpeg (NVDEC 硬解)" : "FFmpeg";
    }

private:
    // GPU 侧缩放链：
    //   buffer(cuda) → scale_cuda(w=…:h=…:format=nv12) → hwdownload → format=nv12 → buffersink
    // 出来的是**目标尺寸的 NV12，在系统内存里**；色彩转换留给 sws（1080p 的量级，比在
    // 4K 上转便宜约 4 倍）。两个实测坑：
    //   1) scale_cuda 只接受 nv12 之类的 yuv 输出，format=bgra/rgba 会在 config 时失败；
    //   2) hwdownload 的输出只能是硬解帧的 sw_format，直接接 format=bgra 也配不起来，
    //      必须先钉 format=nv12。
    // 图和尺寸绑死，源或目标变了就重建。
    bool ensure_cuda_graph(int srcW, int srcH, int dstW, int dstH, const AVFrame* like) {
        FfmpegApi& api = FfmpegApi::Get();
        if (graph_ && gSrcW_ == srcW && gSrcH_ == srcH && gDstW_ == dstW && gDstH_ == dstH) {
            return true;
        }
        if (graph_) { api.avfilter_graph_free(&graph_); graphSrc_ = nullptr; graphSink_ = nullptr; }
        gSrcW_ = gSrcH_ = gDstW_ = gDstH_ = 0;

        // 帧池要挂在 buffer 源上，scale_cuda 才知道用哪个 CUDA 设备。池子的权威来源是
        // **解码器**（`ff_get_format` 选中 CUDA 时会派生一个挂到 codec_->hw_frames_ctx）；
        // hwaccel 路径交回来的 AVFrame 常常自己不带 hw_frames_ctx，只看帧就会每次都判定
        // "没有池子"，于是整条 GPU 链静默失效、每帧退回 4K 回读。
        AVBufferRef* pool = nullptr;
        if (codec_ && codec_->hw_frames_ctx) pool = codec_->hw_frames_ctx;
        else if (like && like->hw_frames_ctx) pool = like->hw_frames_ctx;
        if (!pool) {
            wp_log("FFmpeg: no hw_frames_ctx on decoder or frame; GPU scale chain unavailable");
            return false;
        }

        AVFilterGraph* graph = api.avfilter_graph_alloc();
        if (!graph) return false;

        const AVFilter* srcFilter = api.avfilter_get_by_name("buffer");
        const AVFilter* scaleFilter = api.avfilter_get_by_name("scale_cuda");
        const AVFilter* dlFilter = api.avfilter_get_by_name("hwdownload");
        const AVFilter* fmtFilter = api.avfilter_get_by_name("format");
        const AVFilter* sinkFilter = api.avfilter_get_by_name("buffersink");
        if (!srcFilter || !scaleFilter || !dlFilter || !fmtFilter || !sinkFilter) {
            wp_log("FFmpeg: filter chain unavailable (scale_cuda/hwdownload/format/buffersink)");
            api.avfilter_graph_free(&graph);
            return false;
        }

        AVFilterContext* srcCtx = nullptr;
        AVFilterContext* scaleCtx = nullptr;
        AVFilterContext* dlCtx = nullptr;
        AVFilterContext* fmtCtx = nullptr;
        AVFilterContext* sinkCtx = nullptr;

        // FFmpeg 8.x 改了 buffer 源的初始化检查：**init 时就必须拿到像素格式**，
        // 老的"字符串参数建好、事后补 hw_frames_ctx"两步式在 init 阶段直接
        // EINVAL（滤镜自己的日志是 "Unspecified pixel format"，我们因此整条 GPU 链
        // 静默失效过一轮）。8.x 的正确顺序是三步：
        //   avfilter_graph_alloc_filter（只分配、不 init）
        //   → av_buffersrc_parameters_set（format=CUDA + hw_frames_ctx + 尺寸/时间基一起给）
        //   → avfilter_init_dict（此刻才 init，参数已就位）
        srcCtx = api.avfilter_graph_alloc_filter(graph, srcFilter, "in");
        if (!srcCtx) {
            wp_log("FFmpeg: alloc buffer source failed");
            api.avfilter_graph_free(&graph);
            return false;
        }
        if (AVBufferSrcParameters* par = api.av_buffersrc_parameters_alloc()) {
            par->format = AV_PIX_FMT_CUDA;
            par->width = srcW;
            par->height = srcH;
            par->time_base.num = 1;
            par->time_base.den = 60;      // 节拍由我们自己控，给个正的即可
            par->sample_aspect_ratio.num = 1;
            par->sample_aspect_ratio.den = 1;
            par->hw_frames_ctx = api.av_buffer_ref(pool);
            const int r = api.av_buffersrc_parameters_set(srcCtx, par);
            api.av_buffer_unref(&par->hw_frames_ctx);
            api.av_free(par);
            if (r < 0) {
                wp_log("FFmpeg: buffersrc hw_frames_ctx rejected: " + ff_err(r));
                api.avfilter_graph_free(&graph);
                return false;
            }
        }
        if (const int r = api.avfilter_init_dict(srcCtx, nullptr); r < 0) {
            wp_log("FFmpeg: init buffer source failed: " + ff_err(r));
            api.avfilter_graph_free(&graph);
            return false;
        }

        // 缩放留在 NV12（GPU 上做），回读后仍然是 NV12，转 BGRA 交给 sws
        char args[64]{};
        snprintf(args, sizeof(args), "w=%d:h=%d:format=nv12", dstW, dstH);
        if (api.avfilter_graph_create_filter(&scaleCtx, scaleFilter, "scale", args, nullptr,
                                             graph) < 0 ||
            api.avfilter_graph_create_filter(&dlCtx, dlFilter, "dl", nullptr, nullptr, graph) < 0 ||
            api.avfilter_graph_create_filter(&fmtCtx, fmtFilter, "fmt", "pix_fmts=nv12", nullptr,
                                             graph) < 0 ||
            api.avfilter_graph_create_filter(&sinkCtx, sinkFilter, "out", nullptr, nullptr,
                                             graph) < 0) {
            wp_log("FFmpeg: GPU filter chain filters could not be created");
            api.avfilter_graph_free(&graph);
            return false;
        }
        if (api.avfilter_link(srcCtx, 0, scaleCtx, 0) < 0 ||
            api.avfilter_link(scaleCtx, 0, dlCtx, 0) < 0 ||
            api.avfilter_link(dlCtx, 0, fmtCtx, 0) < 0 ||
            api.avfilter_link(fmtCtx, 0, sinkCtx, 0) < 0) {
            wp_log("FFmpeg: GPU filter chain linking failed");
            api.avfilter_graph_free(&graph);
            return false;
        }
        if (const int r = api.avfilter_graph_config(graph, nullptr); r < 0) {
            wp_log("FFmpeg: GPU filter chain config failed: " + ff_err(r));
            api.avfilter_graph_free(&graph);
            return false;
        }

        graph_ = graph;
        graphSrc_ = srcCtx;
        graphSink_ = sinkCtx;
        gSrcW_ = srcW;
        gSrcH_ = srcH;
        gDstW_ = dstW;
        gDstH_ = dstH;
        wp_log("FFmpeg: GPU filter chain ready " + std::to_string(srcW) + "x" +
               std::to_string(srcH) + " -> " + std::to_string(dstW) + "x" + std::to_string(dstH));
        return true;
    }

    // 释放滤镜图（回卷/关闭时用）：下一帧会用解码器**当前**的帧池重建
    void drop_cuda_graph() {
        FfmpegApi& api = FfmpegApi::Get();
        if (graph_) api.avfilter_graph_free(&graph_);
        graphSrc_ = nullptr;
        graphSink_ = nullptr;
        gSrcW_ = gSrcH_ = gDstW_ = gDstH_ = 0;
    }

    bool gpu_scale_to_nv12(const ImageSize& target) {
        FfmpegApi& api = FfmpegApi::Get();
        if (!ensure_cuda_graph(frame_->width, frame_->height, target.width, target.height,
                               frame_)) {
            // 只试一次：链配不起来就整条关掉，改走"整帧回读 + sws"。否则每一拍都要
            // 先失败一遍再回读，账单会全额落在帧率上，而且没人会告诉你它坏了。
            useCudaChain_ = false;
            return false;
        }

        if (api.av_buffersrc_add_frame_flags(graphSrc_, frame_, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            // 帧池对不上（典型：回卷后解码器换了池）。重建一次，仍不行才走回读。
            if (!chainRebuildTried_) {
                chainRebuildTried_ = true;
                wp_log("FFmpeg: buffersrc rejected a frame; rebuilding GPU filter chain");
                drop_cuda_graph();
                if (ensure_cuda_graph(frame_->width, frame_->height, target.width, target.height,
                                      frame_)) {
                    if (api.av_buffersrc_add_frame_flags(graphSrc_, frame_,
                                                         AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                        chainRebuildTried_ = false;
                        return pull_filtered_nv12();
                    }
                }
            }
            return false;
        }
        return pull_filtered_nv12();
    }

    bool pull_filtered_nv12() {
        FfmpegApi& api = FfmpegApi::Get();
        if (!filterOut_) filterOut_ = api.av_frame_alloc();
        if (!filterOut_) return false;
        api.av_frame_unref(filterOut_);
        if (api.av_buffersink_get_frame(graphSink_, filterOut_) < 0) {
            // 正常应当 1:1 出帧；拉不到说明链路状态在漂移，只报一次但要看得见
            if (!sinkFailLogged_) {
                sinkFailLogged_ = true;
                wp_log("FFmpeg: buffersink returned no frame (first occurrence)");
            }
            ++sinkFails_;
            return false;
        }

        return filterOut_->format == AV_PIX_FMT_NV12 && filterOut_->width > 0 &&
               filterOut_->height > 0;
    }

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

        // 首帧把真实格式写进日志：`[NVDEC]` 只说明我们*请求*了硬解，帧到底在不在显存里
        // 只有这里看得出来（曾经出现过日志报硬解、实际每帧都在 CPU 上转 4K 的情况）
        if (!firstFormatLogged_) {
            firstFormatLogged_ = true;
            const char* fmt = api.av_get_pix_fmt_name
                ? api.av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame_->format)) : nullptr;
            wp_log(std::string("FFmpeg: first frame format=") + (fmt ? fmt : "?") +
                   (frame_->format == AV_PIX_FMT_CUDA ? " (显存帧)" : " (系统内存帧)"));
        }

        // 缩放目标按当前帧尺寸算：流中分辨率变化时也能跟上
        const ImageSize target = decode_target_size(w, h, maxW_, maxH_);

        // 硬解帧优先走 GPU 链：解码和缩小都在显存里做完，回读的只有目标尺寸那份 NV12，
        // CPU 只剩 1080p 的 NV12→BGRA（比在 4K 上转便宜约 4 倍）。
        const AVFrame* src = frame_;
        if (frame_->format == AV_PIX_FMT_CUDA) {
            const bool chainOk = useCudaChain_ && gpu_scale_to_nv12(target);
            ++cudaFrames_;
            if (chainOk) ++cudaChainFrames_;
            if (cudaFrames_ % 120 == 0) {
                wp_log("FFmpeg: cuda frames=" + std::to_string(cudaFrames_) +
                       " chain=" + std::to_string(cudaChainFrames_) +
                       " fallback=" + std::to_string(cudaFrames_ - cudaChainFrames_) +
                       " sinkFail=" + std::to_string(sinkFails_));
            }
            if (chainOk) {
                src = filterOut_;
            } else {
                // 退路：整帧搬回内存的 NV12，再走下面的 sws（CPU 更忙，但一定能出图）
                if (!swFrame_) swFrame_ = api.av_frame_alloc();
                if (!swFrame_) return false;
                api.av_frame_unref(swFrame_);
                if (api.av_hwframe_transfer_data(swFrame_, frame_, 0) < 0) return false;
                src = swFrame_;
            }
        }
        const int srcW = src->width > 0 ? src->width : w;
        const int srcH = src->height > 0 ? src->height : h;

        if (!sws_ || swsSrcW_ != srcW || swsSrcH_ != srcH || swsSrcFmt_ != src->format ||
            swsDstW_ != target.width || swsDstH_ != target.height) {
            if (sws_) api.sws_freeContext(sws_);
            sws_ = api.sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(src->format),
                                      target.width, target.height, AV_PIX_FMT_BGRA, SWS_BILINEAR,
                                      nullptr, nullptr, nullptr);
            swsSrcW_ = srcW;
            swsSrcH_ = srcH;
            swsSrcFmt_ = src->format;
            swsDstW_ = target.width;
            swsDstH_ = target.height;
            if (!sws_) return false;
        }

        const int stride = target.width * 4;
        bgra.resize(static_cast<size_t>(stride) * static_cast<size_t>(target.height));
        uint8_t* dst[4] = { bgra.data(), nullptr, nullptr, nullptr };
        const int dstStride[4] = { stride, 0, 0, 0 };
        const int scaled = api.sws_scale(sws_, src->data, src->linesize, 0, srcH, dst, dstStride);
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
    // 硬解帧搬回内存后的落脚处（NV12/系统内存）；软件路径下不用
    AVFrame* swFrame_ = nullptr;
    AVBufferRef* hwDevice_ = nullptr;
    bool useNvdec_ = false;
    // 解码器带上了 CUDA 设备与格式回调（建了 CUDA 设备不等于在硬解）
    bool hwDecoder_ = false;
    // 首帧的真实像素格式只记一次（诊断用）
    bool firstFormatLogged_ = false;
    // 64 帧保护循环被耗尽只报一次（说明转换链坏了，不是素材问题）
    bool guardWarned_ = false;
    // GPU 缩放链（scale_cuda → hwdownload → format=nv12）
    bool useCudaChain_ = false;
    // buffersrc 拒帧后的一次性重建尝试（防止失败循环）
    bool chainRebuildTried_ = false;
    // 链命中统计与首次 sink 失败标记（诊断"越跑越卡"用）
    int64_t cudaFrames_ = 0;
    int64_t cudaChainFrames_ = 0;
    int64_t sinkFails_ = 0;
    bool sinkFailLogged_ = false;
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* graphSrc_ = nullptr;
    AVFilterContext* graphSink_ = nullptr;
    AVFrame* filterOut_ = nullptr;
    int gSrcW_ = 0, gSrcH_ = 0, gDstW_ = 0, gDstH_ = 0;
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
    // Start 在 video_subsystem_start 之前失败时，Stop 仍会无条件走到这里；
    // 计数为 0 再递减会下溢成 -1，下一次 start 的 fetch_add 返回 -1 就跳过 MFStartup，
    // 之后所有 MF 调用永久失败——这里把下溢挡死。
    int prev = g_mfRefs.fetch_sub(1);
    if (prev <= 0) {
        g_mfRefs.store(0);
        return;
    }
    if (prev == 1) {
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
        auto ff = std::make_unique<FfmpegVideoSource>(options.maxWidth, options.maxHeight,
                                                     options.useNvdec);
        if (ff->Open(path)) return ff;
#endif
        return nullptr;
    };

    if (options.preferFfmpeg) {
        if (auto ff = try_ffmpeg()) {
            if (chosenBackend) *chosenBackend = ff->Backend();
            return ff;
        }
        auto mf = std::make_unique<MfVideoSource>(options.maxWidth, options.maxHeight,
                                                  options.d3dDevice);
        if (mf->Open(path)) {
            wp_log("animated image: FFmpeg could not play it, using MF (first frame only)");
            if (chosenBackend) *chosenBackend = mf->Backend();
            return mf;
        }
        wp_log("no decoder could open the animated image: " + to_utf8(path));
        return nullptr;
    }

    // 要 FFmpeg 硬解（NVDEC）时先走 FFmpeg：这是"硬件优先"里最快的一条
    // （解码在显存里完成，只有色彩转换还在 CPU）。打不开再往下退。
    if (options.useNvdec) {
        if (auto ff = try_ffmpeg()) {
            if (chosenBackend) *chosenBackend = ff->Backend();
            return ff;
        }
        wp_log("FFmpeg/NVDEC could not open this media; falling back");
    }

    auto mf = std::make_unique<MfVideoSource>(options.maxWidth, options.maxHeight,
                                             options.d3dDevice);
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
