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
    HMODULE filter = load(L"avfilter-");
    // avfilter 只用于"GPU 侧缩放/转换"这条加速链；它缺失不该让解码整体不可用，
    // 所以不放进下面这个必要条件里，后面单独看一眼。
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
    // 硬件解码（NVDEC）相关：都在 avutil 里
    DSTK_BIND(util, av_hwdevice_ctx_create);
    DSTK_BIND(util, av_hwframe_transfer_data);
    DSTK_BIND(util, av_buffer_ref);
    DSTK_BIND(util, av_buffer_unref);
    DSTK_BIND(util, av_get_pix_fmt_name);
    DSTK_BIND(util, av_strerror);
    DSTK_BIND(util, av_free);
#undef DSTK_BIND

    // 滤镜链（GPU 侧缩放/像素格式转换）是可选的加速路径：avfilter 缺失或符号不全时
    // 只关掉这条链，解码与其它功能照旧，不能让整体加载失败。
    bool filterOk = (filter != nullptr);
    if (filter) {
#define DSTK_BIND_F(name)                                                        \
    do {                                                                         \
        name = reinterpret_cast<decltype(name)>(GetProcAddress(filter, #name));   \
        if (!name) { wp_log("ffmpeg symbol missing: " #name); filterOk = false; } \
    } while (0)
        DSTK_BIND_F(avfilter_get_by_name);
        DSTK_BIND_F(avfilter_graph_alloc);
        DSTK_BIND_F(avfilter_graph_alloc_filter);
        DSTK_BIND_F(avfilter_init_dict);
        DSTK_BIND_F(avfilter_graph_create_filter);
        DSTK_BIND_F(avfilter_link);
        DSTK_BIND_F(avfilter_graph_config);
        DSTK_BIND_F(avfilter_graph_free);
        DSTK_BIND_F(av_buffersrc_add_frame_flags);
        DSTK_BIND_F(av_buffersrc_parameters_alloc);
        DSTK_BIND_F(av_buffersrc_parameters_set);
        DSTK_BIND_F(av_buffersink_get_frame);
#undef DSTK_BIND_F
    } else {
        wp_log("avfilter unavailable; GPU-side scale/convert chain disabled");
    }
    filter_ok_ = filterOk;

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
