#pragma once

// 解码/渲染路径的字符串映射（纯函数）。
// 这个字符串要**持久化**（wallpaper.json 的 settings.decodePath），所以映射必须稳定：
// 改动等于改数据格式，旧值必须仍能读回默认档。

#include <string>

#include "Types.h"

namespace desktopsticker::wallpaper {

inline const char* decode_path_to_string(DecodePath p) {
    switch (p) {
        case DecodePath::FfmpegHardware:     return "ffmpeg-hw";
        case DecodePath::MediaFoundationD3d: return "mf-d3d11";
        case DecodePath::Cpu:                return "cpu";
        case DecodePath::Auto:               break;
    }
    return "auto";
}

// 未知/缺失一律回落到 Auto：配置文件可能被手改，不能因此打不开。
// "ffmpeg-vulkan" 是早期版本的写法（当时打算走 Vulkan Video），仍按同一档读回，
// 避免用户已经落盘的配置失效。
inline DecodePath decode_path_from_string(const std::string& v) {
    if (v == "ffmpeg-hw" || v == "ffmpeg-vulkan") return DecodePath::FfmpegHardware;
    if (v == "mf-d3d11") return DecodePath::MediaFoundationD3d;
    if (v == "cpu") return DecodePath::Cpu;
    return DecodePath::Auto;
}

// 设置页显示用的名字
inline const wchar_t* decode_path_name(DecodePath p) {
    switch (p) {
        case DecodePath::FfmpegHardware:     return L"FFmpeg + NVIDIA 硬解（NVDEC）";
        case DecodePath::MediaFoundationD3d: return L"Media Foundation + D3D11 硬解";
        case DecodePath::Cpu:                return L"CPU 软解（兼容性最好）";
        case DecodePath::Auto:               break;
    }
    return L"自动（硬件优先）";
}

} // namespace desktopsticker::wallpaper
