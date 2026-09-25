#include "pch.h"
#include "ImageSequenceBackend.h"

#include "Log.h"
#include "Utf8.h"
#include "desktopsticker/wallpaper/BackendKind.h"
#include "desktopsticker/wallpaper/DecodeTarget.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace desktopsticker::wallpaper {

namespace {

// 序列没有自带帧率，取规格 §6 的默认值；调速由 frame_advance_policy 缩放它
constexpr int64_t kSequenceFrameMs = 100;

} // namespace

int64_t ImageSequenceBackend::qpc_us() const {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000LL / freq;
}

bool ImageSequenceBackend::Open(const BackendRequest& request, const BackendContext& ctx) {
    Close();

    speed_ = clamp_speed(request.speed);
    maxW_ = ctx.width;
    maxH_ = ctx.height;

    std::error_code ec;
    const fs::path src(request.sourcePath);
    if (fs::is_directory(src, ec)) {
        std::vector<std::wstring> names;
        for (const auto& e : fs::directory_iterator(src, ec)) {
            if (e.is_regular_file(ec)) names.push_back(e.path().filename().wstring());
        }
        if (ec) {
            lastError_ = "enumerate frames failed";
            wp_log("image sequence: enumerate failed for " + to_utf8(src.wstring()));
            return false;
        }

        const auto images = natural_sort_image_frames(names);
        if (images.empty()) {
            lastError_ = "no image frames";
            wp_log("image sequence: no image frames in " + to_utf8(src.wstring()));
            return false;
        }
        if (images.size() != names.size()) {
            // 规格 §13：非图片文件跳过并记数，不当成错误
            wp_log("image sequence: skipped " + std::to_string(names.size() - images.size()) +
                   " non-image file(s)");
        }
        frames_.reserve(images.size());
        for (const auto& n : images) frames_.push_back((src / n).wstring());
    } else if (fs::is_regular_file(src, ec)) {
        // 单张静态图片：序列长度 1，作为静态壁纸播放
        frames_.push_back(src.wstring());
    } else {
        lastError_ = "source not found";
        wp_log("image sequence: source not found: " + to_utf8(request.sourcePath));
        return false;
    }

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic_)))) {
        // 老系统上没有 WICImagingFactory2，退回 v1 工厂
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&wic_)))) {
            lastError_ = "WIC unavailable";
            wp_log("image sequence: WIC factory unavailable");
            Close();
            return false;
        }
    }

    index_ = 0;
    lastTickUs_ = qpc_us();
    wp_log("image sequence opened: " + std::to_string(frames_.size()) + " frame(s) from " +
           to_utf8(request.sourcePath));
    return true;
}

void ImageSequenceBackend::Close() {
    frames_.clear();
    index_ = 0;
    wic_.Reset();
    lastW_ = lastH_ = 0;
    advanceState_.Reset();
}

void ImageSequenceBackend::SetPaused(bool paused) {
    paused_ = paused;
    // 恢复时重置计时，避免把暂停时长当成一次巨大间隔
    if (!paused) lastTickUs_ = qpc_us();
}

void ImageSequenceBackend::SetSpeed(double speed) {
    speed_ = clamp_speed(speed);
}

double ImageSequenceBackend::TargetFps() const {
    return 1000.0 / static_cast<double>(kSequenceFrameMs);
}

bool ImageSequenceBackend::ProduceFrame(std::vector<uint8_t>& bgra, int& w, int& h) {
    if (frames_.empty() || paused_) return false;

    const int64_t now = qpc_us();
    const int64_t elapsedMs = (now - lastTickUs_) / 1000;
    lastTickUs_ = now;

    const FrameAdvance adv = frame_advance_policy(advanceState_, speed_, elapsedMs,
                                                 kSequenceFrameMs);
    if (adv.consume == 0) {
        if (lastW_ <= 0 || lastH_ <= 0) return false;   // 还没出过帧
        w = lastW_;
        h = lastH_;
        return true;                                     // 缓冲区里已是上一帧，零拷贝
    }

    // 单张图片时 consume 会一直推进，取模后仍是同一帧，等价于静态壁纸
    index_ = (index_ + static_cast<size_t>(adv.consume)) % frames_.size();

    if (!decode_current(bgra, w, h)) return false;
    lastW_ = w;
    lastH_ = h;
    ++serial_;
    return w > 0 && h > 0;
}

bool ImageSequenceBackend::decode_current(std::vector<uint8_t>& bgra, int& w, int& h) {
    if (!wic_ || frames_.empty()) return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic_->CreateDecoderFromFilename(frames_[index_].c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &decoder))) {
        lastError_ = "decode open failed";
        wp_log("image sequence: cannot decode " + to_utf8(frames_[index_]));
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    UINT srcW = 0, srcH = 0;
    if (FAILED(frame->GetSize(&srcW, &srcH)) || srcW == 0 || srcH == 0) return false;

    // 与视频路径同一个判断：源比屏幕大就在解码时就缩掉，别让 CPU 白转
    const ImageSize target = decode_target_size(static_cast<int>(srcW), static_cast<int>(srcH),
                                               maxW_, maxH_);

    Microsoft::WRL::ComPtr<IWICBitmapSource> source = frame;
    if (target.width != static_cast<int>(srcW) || target.height != static_cast<int>(srcH)) {
        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(wic_->CreateBitmapScaler(&scaler))) return false;
        if (FAILED(scaler->Initialize(frame.Get(), static_cast<UINT>(target.width),
                                      static_cast<UINT>(target.height),
                                      WICBitmapInterpolationModeFant))) {
            return false;
        }
        source = scaler;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic_->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }

    const int stride = target.width * 4;
    bgra.resize(static_cast<size_t>(stride) * static_cast<size_t>(target.height));
    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                                     static_cast<UINT>(bgra.size()), bgra.data()))) {
        lastError_ = "CopyPixels failed";
        return false;
    }

    w = target.width;
    h = target.height;
    return true;
}

} // namespace desktopsticker::wallpaper
