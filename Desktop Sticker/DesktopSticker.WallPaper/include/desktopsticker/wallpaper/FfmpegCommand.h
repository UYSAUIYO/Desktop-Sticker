#pragma once

#include <string>
#include <vector>

#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

inline std::wstring variant_file_name(VariantKind kind, int revision) {
    const wchar_t* prefix = (kind == VariantKind::PowerSaver) ? L"power-saver" : L"balanced";
    return std::wstring(prefix) + L"-v" + std::to_wstring(revision) + L".mp4";
}

// 档位对应的目标码率（bps）。用位率而非 CRF：CRF 与 -preset 是 x264 专属，
// 而固定的 LGPL 构建没有 x264（含 x264 会变成 GPL），只有 libopenh264 与硬件编码器。
inline int variant_bitrate(VariantKind kind) {
    return (kind == VariantKind::PowerSaver) ? 2'000'000 : 6'000'000;
}

// 生成性能副本的 ffmpeg 参数。恒定输出到派生文件，绝不指向源文件（产品不变量）。
// encoder 由调用方按实测可用性选择（见 FfmpegTranscoder）。
inline std::vector<std::wstring> build_transcode_args(const std::wstring& input,
                                                      const std::wstring& output,
                                                      VariantKind kind,
                                                      const std::wstring& encoder) {
    if (kind == VariantKind::Original) return {};

    return {
        L"-hide_banner", L"-loglevel", L"error",
        L"-nostdin",
        L"-y",                                   // 只覆盖我们自己的派生文件
        L"-i", input,
        L"-c:v", encoder,
        L"-b:v", std::to_wstring(variant_bitrate(kind)),
        L"-pix_fmt", L"yuv420p",
        L"-movflags", L"+faststart",
        L"-an",                                  // 壁纸静音
        output,
    };
}

} // namespace desktopsticker::wallpaper
