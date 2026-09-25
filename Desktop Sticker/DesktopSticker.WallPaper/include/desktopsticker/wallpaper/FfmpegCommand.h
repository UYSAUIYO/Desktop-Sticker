#pragma once

#include <string>
#include <vector>

#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

inline std::wstring variant_file_name(VariantKind kind, int revision) {
    const wchar_t* prefix = (kind == VariantKind::PowerSaver) ? L"power-saver" : L"balanced";
    return std::wstring(prefix) + L"-v" + std::to_wstring(revision) + L".mp4";
}

// 生成性能副本的 ffmpeg 参数。恒定输出到派生文件，绝不指向源文件（产品不变量）。
inline std::vector<std::wstring> build_transcode_args(const std::wstring& input,
                                                      const std::wstring& output,
                                                      VariantKind kind) {
    if (kind == VariantKind::Original) return {};

    const int crf = (kind == VariantKind::PowerSaver) ? 28 : 23;
    return {
        L"-hide_banner", L"-loglevel", L"error",
        L"-nostdin",
        L"-y",                                   // 只覆盖我们自己的派生文件
        L"-i", input,
        L"-c:v", L"libx264",
        L"-preset", L"veryfast",
        L"-crf", std::to_wstring(crf),
        L"-pix_fmt", L"yuv420p",
        L"-movflags", L"+faststart",
        L"-an",                                  // 壁纸静音
        output,
    };
}

} // namespace desktopsticker::wallpaper
