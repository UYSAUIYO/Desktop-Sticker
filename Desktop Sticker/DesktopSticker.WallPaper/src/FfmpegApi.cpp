#include "pch.h"
#include "FfmpegApi.h"

#include "Log.h"
#include "Utf8.h"

#include <mutex>

namespace desktopsticker::wallpaper {

#ifdef DSTK_HAVE_FFMPEG_SDK

namespace {

std::mutex g_loadMutex;

// 在负载目录里找形如 avformat-<n>.dll 的库：这样"兼容的替代共享库"也能被接受
// （与 FFmpeg-NOTICE 的措辞一致），同时下载本身已由 SHA-256 锁定版本。
std::wstring find_module(const std::filesystem::path& dir, const std::wstring& prefix) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::wstring name = entry.path().filename().wstring();
        if (entry.path().extension().wstring() != L".dll") continue;
        if (name.rfind(prefix, 0) == 0) return entry.path().wstring();
    }
    return {};
}

} // namespace

bool FfmpegApi::Load(const std::wstring& ffmpegDir) {
    std::lock_guard<std::mutex> lock(g_loadMutex);
    if (loaded_) return true;

    const std::filesystem::path dir(ffmpegDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        wp_log("ffmpeg payload directory missing: " + to_utf8(ffmpegDir));
        return false;
    }

    auto load = [&](const std::wstring& prefix) -> HMODULE {
        const std::wstring path = find_module(dir, prefix);
        if (path.empty()) {
            wp_log("ffmpeg module not found for prefix " + to_utf8(prefix));
            return nullptr;
        }
        HMODULE m = LoadLibraryExW(path.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!m) {
            wp_log("LoadLibraryExW failed (" + std::to_string(GetLastError()) + "): " + to_utf8(path));
        }
        return m;
    };

    HMODULE util = load(L"avutil-");
    HMODULE codec = load(L"avcodec-");
    HMODULE format = load(L"avformat-");
    HMODULE scale = load(L"swscale-");
    HMODULE resample = load(L"swresample-");
    if (!util || !codec || !format || !scale || !resample) return false;

    bool ok = true;
#define DSTK_BIND(module, name)                                                  \
    do {                                                                         \
        name = reinterpret_cast<decltype(name)>(GetProcAddress(module, #name));   \
        if (!name) {                                                             \
            wp_log("ffmpeg symbol missing: " #name);                             \
            ok = false;                                                          \
        }                                                                        \
    } while (0)

    DSTK_BIND(format, avformat_alloc_context);
    DSTK_BIND(format, avformat_open_input);
    DSTK_BIND(format, avformat_find_stream_info);
    DSTK_BIND(format, av_find_best_stream);
    DSTK_BIND(format, av_read_frame);
    DSTK_BIND(format, av_seek_frame);
    DSTK_BIND(format, avformat_close_input);
    DSTK_BIND(codec, avcodec_find_decoder);
    DSTK_BIND(codec, avcodec_alloc_context3);
    DSTK_BIND(codec, avcodec_parameters_to_context);
    DSTK_BIND(codec, avcodec_open2);
    DSTK_BIND(codec, avcodec_send_packet);
    DSTK_BIND(codec, avcodec_receive_frame);
    DSTK_BIND(codec, avcodec_flush_buffers);
    DSTK_BIND(codec, avcodec_free_context);
    DSTK_BIND(codec, av_packet_alloc);
    DSTK_BIND(codec, av_packet_free);
    DSTK_BIND(codec, av_packet_unref);
    DSTK_BIND(util, av_frame_alloc);
    DSTK_BIND(util, av_frame_free);
    DSTK_BIND(util, av_frame_unref);
    DSTK_BIND(scale, sws_getContext);
    DSTK_BIND(scale, sws_scale);
    DSTK_BIND(scale, sws_freeContext);
    DSTK_BIND(resample, swr_alloc_set_opts2);
    DSTK_BIND(resample, swr_init);
    DSTK_BIND(resample, swr_convert);
    DSTK_BIND(resample, swr_free);
#undef DSTK_BIND

    if (!ok) return false;

    loaded_ = true;
    wp_log("ffmpeg shared libraries loaded; built-in fallback decoder available");
    return true;
}

#else // 构建期没有 FFmpeg SDK

bool FfmpegApi::Load(const std::wstring&) {
    wp_log("ffmpeg sdk absent at build time; built-in fallback decoder disabled "
           "(run tools/prepare_ffmpeg.ps1 and rebuild for the full pipeline)");
    return false;
}

#endif

FfmpegApi& FfmpegApi::Get() {
    static FfmpegApi api;
    return api;
}

} // namespace desktopsticker::wallpaper
