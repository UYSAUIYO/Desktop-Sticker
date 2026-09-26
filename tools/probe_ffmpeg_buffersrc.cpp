// 探针：复现并定位 avfilter buffer 源在 FFmpeg 8.1 上 "Invalid argument" 的原因。
// 与 WallPaper 的 FfmpegApi 一样走 GetProcAddress，不依赖导入库。
// 编译（VS2022 x64）：
//   cl /nologo /EHsc /std:c++20 /utf-8 /I tools/ffmpeg-sdk/include tools/probe_ffmpeg_buffersrc.cpp
//     （链接：uuid.lib 等系统库，DLL 运行时加载）
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libswscale/swscale.h"
}

static HMODULE g_util, g_codec, g_format, g_filter, g_scale;

// 两段式绑定：先收集 (模块, 符号名, 槽位)，DLL 加载完成后统一 GetProcAddress
struct Binding { const char* mod; const char* name; void** slot; };
static Binding g_bindings[64];
static int g_bindingCount = 0;

#define DECL_FN(name) static decltype(name)* fn_##name = nullptr
DECL_FN(av_hwdevice_ctx_create);
DECL_FN(av_buffer_ref);
DECL_FN(av_buffer_unref);
DECL_FN(av_strerror);
DECL_FN(av_get_pix_fmt_name);
DECL_FN(av_free);
DECL_FN(avcodec_find_decoder);
DECL_FN(avcodec_alloc_context3);
DECL_FN(avcodec_parameters_to_context);
DECL_FN(avcodec_open2);
DECL_FN(avcodec_send_packet);
DECL_FN(avcodec_receive_frame);
DECL_FN(avcodec_free_context);
DECL_FN(av_packet_alloc);
DECL_FN(av_packet_free);
DECL_FN(av_frame_alloc);
DECL_FN(av_frame_free);
DECL_FN(av_frame_unref);
DECL_FN(av_read_frame);
DECL_FN(avformat_open_input);
DECL_FN(avformat_find_stream_info);
DECL_FN(avformat_close_input);
DECL_FN(avfilter_get_by_name);
DECL_FN(avfilter_graph_alloc);
DECL_FN(avfilter_graph_create_filter);
DECL_FN(avfilter_graph_alloc_filter);
DECL_FN(avfilter_init_str);
DECL_FN(avfilter_init_dict);
DECL_FN(avfilter_link);
DECL_FN(avfilter_graph_config);
DECL_FN(avfilter_graph_free);
DECL_FN(av_buffersrc_add_frame_flags);
DECL_FN(av_buffersrc_parameters_alloc);
DECL_FN(av_buffersrc_parameters_set);
DECL_FN(av_buffersink_get_frame);
DECL_FN(sws_getContext);
DECL_FN(sws_scale);
DECL_FN(sws_freeContext);
#undef DECL_FN

#define REG(mod, name) g_bindings[g_bindingCount++] = {#mod, #name, (void**)&fn_##name}
static void register_bindings() {
    REG(util, av_hwdevice_ctx_create);
    REG(util, av_buffer_ref);
    REG(util, av_buffer_unref);
    REG(util, av_strerror);
    REG(util, av_get_pix_fmt_name);
    REG(util, av_free);
    REG(codec, avcodec_find_decoder);
    REG(codec, avcodec_alloc_context3);
    REG(codec, avcodec_parameters_to_context);
    REG(codec, avcodec_open2);
    REG(codec, avcodec_send_packet);
    REG(codec, avcodec_receive_frame);
    REG(codec, avcodec_free_context);
    REG(codec, av_packet_alloc);
    REG(codec, av_packet_free);
    REG(util, av_frame_alloc);
    REG(util, av_frame_free);
    REG(util, av_frame_unref);
    REG(format, av_read_frame);
    REG(format, avformat_open_input);
    REG(format, avformat_find_stream_info);
    REG(format, avformat_close_input);
    REG(filter, avfilter_get_by_name);
    REG(filter, avfilter_graph_alloc);
    REG(filter, avfilter_graph_create_filter);
    REG(filter, avfilter_graph_alloc_filter);
    REG(filter, avfilter_init_str);
    REG(filter, avfilter_init_dict);
    REG(filter, avfilter_link);
    REG(filter, avfilter_graph_config);
    REG(filter, avfilter_graph_free);
    REG(filter, av_buffersrc_add_frame_flags);
    REG(filter, av_buffersrc_parameters_alloc);
    REG(filter, av_buffersrc_parameters_set);
    REG(filter, av_buffersink_get_frame);
    REG(scale, sws_getContext);
    REG(scale, sws_scale);
    REG(scale, sws_freeContext);
}
#undef REG

static bool load_dlls_and_bind() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t pos = dir.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return false;
    dir = dir.substr(0, pos + 1) + L"ffmpeg\\";   // exe 在 tools/，DLL 在 tools/ffmpeg/

    auto load = [&](const wchar_t* name) -> HMODULE {
        std::wstring full = dir + name;
        HMODULE m = LoadLibraryExW(full.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                   LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!m) printf("  !! load failed: %ls (err=%lu)\n", name, GetLastError());
        return m;
    };
    g_util = load(L"avutil-60.dll");        // exe 在 tools/，DLL 同目录
    g_codec = load(L"avcodec-62.dll");
    g_format = load(L"avformat-62.dll");
    g_filter = load(L"avfilter-11.dll");
    g_scale = load(L"swscale-9.dll");
    if (!g_util || !g_codec || !g_format || !g_filter || !g_scale) return false;

    register_bindings();
    auto pick = [&](const char* mod) -> HMODULE {
        if (!strcmp(mod, "util")) return g_util;
        if (!strcmp(mod, "codec")) return g_codec;
        if (!strcmp(mod, "format")) return g_format;
        if (!strcmp(mod, "filter")) return g_filter;
        if (!strcmp(mod, "scale")) return g_scale;
        return nullptr;
    };
    bool ok = true;
    for (int i = 0; i < g_bindingCount; ++i) {
        HMODULE m = pick(g_bindings[i].mod);
        void* p = m ? (void*)GetProcAddress(m, g_bindings[i].name) : nullptr;
        *g_bindings[i].slot = p;
        if (!p) { printf("  !! symbol missing: %s (in %s)\n", g_bindings[i].name, g_bindings[i].mod); ok = false; }
    }
    return ok;
}

static std::string errtext(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    if (fn_av_strerror && fn_av_strerror(code, buf, sizeof(buf)) == 0) return buf;
    return "code " + std::to_string(code);
}

// ---- 变体列表：找到 8.1 接受的 buffer 源参数写法 ----
struct Variant { const char* label; const char* args; };
static const Variant kVariants[] = {
    {"A: app 原样 (video_size+time_base+pixel_aspect)",
     "video_size=3840x2160:time_base=1/60:pixel_aspect=1/1"},
    {"B: 原样 + frame_rate",
     "video_size=3840x2160:time_base=1/60:pixel_aspect=1/1:frame_rate=60/1"},
    {"C: width/height 代替 video_size",
     "width=3840:height=2160:time_base=1/60:pixel_aspect=1/1"},
    {"D: 只有 video_size+time_base",
     "video_size=3840x2160:time_base=1/60"},
    {"E: 只有 video_size",
     "video_size=3840x2160"},
    {"F: 空参数", ""},
};

static void try_create_variants() {
    printf("=== 1) buffer 源创建变体 ===\n");
    for (const Variant& v : kVariants) {
        AVFilterGraph* graph = fn_avfilter_graph_alloc();
        const AVFilter* src = fn_avfilter_get_by_name("buffer");
        AVFilterContext* ctx = nullptr;
        int r = fn_avfilter_graph_create_filter(&ctx, src, "in", v.args, nullptr, graph);
        printf("  %s -> %d (%s)\n", v.label, r, r < 0 ? errtext(r).c_str() : "ok");
        fn_avfilter_graph_free(&graph);
    }
    // alloc_filter + init_str（8.x 推荐写法）
    {
        AVFilterGraph* graph = fn_avfilter_graph_alloc();
        const AVFilter* src = fn_avfilter_get_by_name("buffer");
        AVFilterContext* ctx = fn_avfilter_graph_alloc_filter(graph, src, "in");
        int r = ctx ? fn_avfilter_init_str(ctx, kVariants[0].args) : -999;
        printf("  G: alloc_filter + init_str (app 原样参数) -> %d (%s)\n", r,
               r < 0 ? errtext(r).c_str() : "ok");
        fn_avfilter_graph_free(&graph);
    }
}

// ---- 完整链：真实 NVDEC 解码 buffer -> scale_cuda -> hwdownload -> format=nv12 -> buffersink ----
static AVPixelFormat pick_hw_format(AVCodecContext*, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; p && *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_CUDA) return *p;
    return fmts[0];
}

static bool build_chain(AVFilterGraph** outGraph, AVFilterContext** outSrc,
                        AVFilterContext** outSink, AVCodecContext* codec,
                        int srcW, int srcH, int dstW, int dstH,
                        const char* argsOverride, bool attachPool) {
    AVFilterGraph* graph = fn_avfilter_graph_alloc();
    const AVFilter *fSrc = fn_avfilter_get_by_name("buffer");
    const AVFilter *fScale = fn_avfilter_get_by_name("scale_cuda");
    const AVFilter *fDl = fn_avfilter_get_by_name("hwdownload");
    const AVFilter *fFmt = fn_avfilter_get_by_name("format");
    const AVFilter *fSink = fn_avfilter_get_by_name("buffersink");
    if (!fSrc || !fScale || !fDl || !fFmt || !fSink) {
        printf("  filter missing: buffer/scale_cuda/hwdownload/format/buffersink\n");
        return false;
    }

    char args[256]{};
    if (argsOverride)
        snprintf(args, sizeof(args), "%s", argsOverride);
    else
        snprintf(args, sizeof(args), "video_size=%dx%d:time_base=1/60:pixel_aspect=1/1",
                 srcW, srcH);

    AVFilterContext *src = nullptr, *scale = nullptr, *dl = nullptr, *fmt = nullptr, *sink = nullptr;

    // FFmpeg 8.x 的正确写法：alloc（不 init）→ buffersrc 参数连 hw_frames_ctx 一起给 → init_dict
    src = fn_avfilter_graph_alloc_filter(graph, fSrc, "in");
    if (!src) { printf("  alloc buffer failed\n"); fn_avfilter_graph_free(&graph); return false; }
    {
        AVBufferSrcParameters* par = fn_av_buffersrc_parameters_alloc();
        par->format = AV_PIX_FMT_CUDA;
        par->width = srcW;
        par->height = srcH;
        par->time_base.num = 1; par->time_base.den = 60;
        par->sample_aspect_ratio.num = 1; par->sample_aspect_ratio.den = 1;
        par->hw_frames_ctx = fn_av_buffer_ref(codec->hw_frames_ctx);
        int r = fn_av_buffersrc_parameters_set(src, par);
        fn_av_buffer_unref(&par->hw_frames_ctx);
        fn_av_free(par);
        if (r < 0) { printf("  buffersrc params failed: %s\n", errtext(r).c_str()); fn_avfilter_graph_free(&graph); return false; }
    }
    {
        int r = fn_avfilter_init_dict(src, nullptr);
        if (r < 0) { printf("  init buffer failed: %s\n", errtext(r).c_str()); fn_avfilter_graph_free(&graph); return false; }
    }

    char scaleArgs[128]{};
    snprintf(scaleArgs, sizeof(scaleArgs), "w=%d:h=%d:format=nv12", dstW, dstH);
    if (fn_avfilter_graph_create_filter(&scale, fScale, "scale", scaleArgs, nullptr, graph) < 0 ||
        fn_avfilter_graph_create_filter(&dl, fDl, "dl", nullptr, nullptr, graph) < 0 ||
        fn_avfilter_graph_create_filter(&fmt, fFmt, "fmt", "pix_fmts=nv12", nullptr, graph) < 0 ||
        fn_avfilter_graph_create_filter(&sink, fSink, "out", nullptr, nullptr, graph) < 0) {
        printf("  chain filter create failed\n");
        fn_avfilter_graph_free(&graph);
        return false;
    }
    if (fn_avfilter_link(src, 0, scale, 0) < 0 ||
        fn_avfilter_link(scale, 0, dl, 0) < 0 ||
        fn_avfilter_link(dl, 0, fmt, 0) < 0 ||
        fn_avfilter_link(fmt, 0, sink, 0) < 0) {
        printf("  link failed\n");
        fn_avfilter_graph_free(&graph);
        return false;
    }
    const int rc = fn_avfilter_graph_config(graph, nullptr);
    if (rc < 0) {
        printf("  graph config failed: %s\n", errtext(rc).c_str());
        fn_avfilter_graph_free(&graph);
        return false;
    }
    *outGraph = graph; *outSrc = src; *outSink = sink;
    return true;
}

int main(int argc, char** argv) {
    if (!load_dlls_and_bind()) { printf("dll/symbol load failed\n"); return 1; }
    printf("dlls loaded, all symbols bound\n");

    try_create_variants();

    // ---- 真实解码 + GPU 链端到端 ----
    const char* path = "E:/DesktopSticker/Wallpaper/media/903b4bba-ba78-4b25-bb52-5c0e167413b8/source.mp4";
    if (argc > 1) path = argv[1];
    printf("\n=== 2) NVDEC + scale_cuda 端到端 (%s) ===\n", path);

    AVFormatContext* fmtCtx = nullptr;
    if (fn_avformat_open_input(&fmtCtx, path, nullptr, nullptr) < 0) { printf("open failed\n"); return 1; }
    if (fn_avformat_find_stream_info(fmtCtx, nullptr) < 0) { printf("stream info failed\n"); return 1; }
    int vi = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i)
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vi = (int)i; break; }
    if (vi < 0) { printf("no video stream\n"); return 1; }
    const AVCodec* codec = fn_avcodec_find_decoder(fmtCtx->streams[vi]->codecpar->codec_id);

    AVCodecContext* cc = fn_avcodec_alloc_context3(codec);
    fn_avcodec_parameters_to_context(cc, fmtCtx->streams[vi]->codecpar);

    AVBufferRef* hwDev = nullptr;
    if (fn_av_hwdevice_ctx_create(&hwDev, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0) < 0) {
        printf("cuda device failed\n"); return 1;
    }
    cc->hw_device_ctx = fn_av_buffer_ref(hwDev);
    cc->get_format = pick_hw_format;
    if (fn_avcodec_open2(cc, codec, nullptr) < 0) { printf("decoder open failed\n"); return 1; }
    printf("  decoder opened %dx%d\n", cc->width, cc->height);

    AVPacket* pkt = fn_av_packet_alloc();
    AVFrame* frame = fn_av_frame_alloc();

    // 读到第一帧（拿到 hw_frames_ctx）
    bool gotFirst = false;
    for (int guard = 0; guard < 300 && !gotFirst; ++guard) {
        if (fn_avcodec_receive_frame(cc, frame) == 0) { gotFirst = true; break; }
        if (fn_av_read_frame(fmtCtx, pkt) < 0) break;
        fn_avcodec_send_packet(cc, pkt);
    }
    if (!gotFirst) { printf("no frame decoded\n"); return 1; }
    printf("  first frame: %dx%d format=%s hw_frames_ctx=%s\n", frame->width, frame->height,
           fn_av_get_pix_fmt_name((AVPixelFormat)frame->format),
           cc->hw_frames_ctx ? "yes(codec)" : (frame->hw_frames_ctx ? "yes(frame)" : "no"));

    const int srcW = frame->width, srcH = frame->height;
    const int dstW = 1920, dstH = 1080;

    AVFilterGraph* graph = nullptr; AVFilterContext *src = nullptr, *sink = nullptr;
    const char* tried = nullptr;
    if (build_chain(&graph, &src, &sink, cc, srcW, srcH, dstW, dstH, kVariants[0].args, true)) {
        tried = "app 原样参数";
    } else if (build_chain(&graph, &src, &sink, cc, srcW, srcH, dstW, dstH, kVariants[1].args, true)) {
        tried = "带 frame_rate";
    } else {
        printf("  all chain builds failed\n");
        return 1;
    }
    printf("  GPU 链建立成功 (%s)\n", tried);

    // 端到端计时：解码 -> 链 -> 回读 NV12 -> sws BGRA
    AVFrame* out = fn_av_frame_alloc();
    SwsContext* sws = nullptr;
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    int frames = 0;
    double decodeMs = 0, chainMs = 0, swsMs = 0;
    QueryPerformanceCounter(&t0);
    const int target = 300;
    while (frames < target) {
        LARGE_INTEGER a{}, b{}, c{}, d{};
        QueryPerformanceCounter(&a);
        int r = fn_avcodec_receive_frame(cc, frame);
        if (r < 0) {
            if (fn_av_read_frame(fmtCtx, pkt) < 0) break;
            fn_avcodec_send_packet(cc, pkt);
            continue;
        }
        QueryPerformanceCounter(&b);
        if (fn_av_buffersrc_add_frame_flags(src, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            printf("  buffersrc add failed\n"); break;
        }
        fn_av_frame_unref(out);
        if (fn_av_buffersink_get_frame(sink, out) < 0) { printf("  sink empty\n"); break; }
        QueryPerformanceCounter(&c);
        const int stride = dstW * 4;
        static std::string bgra;
        bgra.resize((size_t)stride * dstH);
        uint8_t* dst[4] = { (uint8_t*)bgra.data(), nullptr, nullptr, nullptr };
        const int dstStride[4] = { stride, 0, 0, 0 };
        sws = fn_sws_getContext(out->width, out->height, (AVPixelFormat)out->format,
                                dstW, dstH, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) { printf("  sws failed\n"); break; }
        fn_sws_scale(sws, out->data, out->linesize, 0, out->height, dst, dstStride);
        fn_sws_freeContext(sws); sws = nullptr;
        QueryPerformanceCounter(&d);
        decodeMs += (b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart;
        chainMs  += (c.QuadPart - b.QuadPart) * 1000.0 / freq.QuadPart;
        swsMs    += (d.QuadPart - c.QuadPart) * 1000.0 / freq.QuadPart;
        fn_av_frame_unref(frame);
        ++frames;
    }
    QueryPerformanceCounter(&t1);
    if (frames > 0) {
        printf("  %d 帧: decode=%.1fms chain=%.1fms sws=%.1fms 总计=%.1fms/帧 (=%.0f fps 上限)\n",
               frames, decodeMs / frames, chainMs / frames, swsMs / frames,
               (decodeMs + chainMs + swsMs) / frames,
               frames * 1000.0 / ((t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart));
    }
    fn_avfilter_graph_free(&graph);
    fn_av_frame_free(&out);
    fn_av_frame_free(&frame);
    fn_av_packet_free(&pkt);
    fn_avcodec_free_context(&cc);
    fn_avformat_close_input(&fmtCtx);
    fn_av_buffer_unref(&hwDev);
    return 0;
}
