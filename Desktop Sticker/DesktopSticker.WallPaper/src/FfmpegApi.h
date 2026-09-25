#pragma once

#include <string>

// 构建期是否具备 FFmpeg 开发头文件（由 tools/prepare_ffmpeg.ps1 抽取自 -shared 包）。
// 缺 SDK 时本文件与 FfmpegVideoSource 仍可编译，只是兜底解码不可用——
// 保证"负载缺失不影响编译"，同时让完整构建拿到真实解码能力。
#if defined(__has_include)
#  if __has_include(<libavformat/avformat.h>) && __has_include(<libavcodec/avcodec.h>) && \
      __has_include(<libswscale/swscale.h>)
#    define DSTK_HAVE_FFMPEG_SDK 1
#  endif
#endif

#ifdef DSTK_HAVE_FFMPEG_SDK
#pragma warning(push)
#pragma warning(disable : 4244) // 固定的 FFmpeg 头文件内联辅助函数有窄化转换
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#pragma warning(pop)
#endif

namespace desktopsticker::wallpaper {

// av*.dll 的动态加载器。
// 合规要求：只动态加载，不静态链接任何 FFmpeg 库；不向 Windows 注册解码器。
// 安全要求：只从负载目录解析（受限搜索路径），不从当前工作目录或 PATH 解析。
class FfmpegApi {
public:
    static FfmpegApi& Get();

    // 幂等；失败返回 false，仅禁用兜底解码，不影响 MF 主路径
    bool Load(const std::wstring& ffmpegDir);
    bool Loaded() const { return loaded_; }

#ifdef DSTK_HAVE_FFMPEG_SDK
    // 只声明实际用到的符号；decltype 取自真实头文件声明，故需构建期 SDK
#define DSTK_AV_FN(name) decltype(&::name) name = nullptr
    DSTK_AV_FN(avformat_alloc_context);
    DSTK_AV_FN(avformat_open_input);
    DSTK_AV_FN(avformat_find_stream_info);
    DSTK_AV_FN(av_find_best_stream);
    DSTK_AV_FN(av_read_frame);
    DSTK_AV_FN(av_seek_frame);
    DSTK_AV_FN(avformat_close_input);
    DSTK_AV_FN(avcodec_find_decoder);
    DSTK_AV_FN(avcodec_alloc_context3);
    DSTK_AV_FN(avcodec_parameters_to_context);
    DSTK_AV_FN(avcodec_open2);
    DSTK_AV_FN(avcodec_send_packet);
    DSTK_AV_FN(avcodec_receive_frame);
    DSTK_AV_FN(avcodec_flush_buffers);
    DSTK_AV_FN(avcodec_free_context);
    DSTK_AV_FN(av_packet_alloc);
    DSTK_AV_FN(av_packet_free);
    DSTK_AV_FN(av_packet_unref);
    DSTK_AV_FN(av_frame_alloc);
    DSTK_AV_FN(av_frame_free);
    DSTK_AV_FN(av_frame_unref);
    DSTK_AV_FN(sws_getContext);
    DSTK_AV_FN(sws_scale);
    DSTK_AV_FN(sws_freeContext);
#undef DSTK_AV_FN
#endif

private:
    FfmpegApi() = default;
    bool loaded_ = false;
};

} // namespace desktopsticker::wallpaper
