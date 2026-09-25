#pragma once

// 本头文件被 EXE 侧与测试工程共同包含，只允许依赖标准库。
// 不得引入 windows.h / D3D / MF 依赖，否则会污染 EXE 与单测的编译环境。
//
// 注意：这里是 EXE ↔ DLL 的 ABI 之一部分（IWallPaperModule 用到本文件的类型），
// 任何字段变更都是 ABI 断裂，两端必须同步重编。

#include <cstdint>
#include <string>

namespace desktopsticker {

// 播放档位：原画（MF→FFmpeg 兜底）或由 ffmpeg.exe 生成的性能副本
enum class VariantKind { Original, Balanced, PowerSaver };

// 壁纸后端类型。决定用哪条渲染/解码管线。
enum class BackendKind {
    Video,          // 视频文件（MF 为主，FFmpeg 兜底）
    AnimatedImage,  // GIF / 动态 WebP / APNG
    ImageSequence,  // 图片文件夹（或单张静态图）
    Web,            // HTML/CSS/JS（WebView2）
    Shader3D,       // Vulkan 着色器 / 3D 模型 / 粒子
};

struct WallPaperItem {
    std::wstring id;           // 库内唯一 id
    std::wstring name;         // 显示名，缺省为源文件主名
    std::wstring sourceFile;   // 相对 media\<id>\ 的源文件名（Video/AnimatedImage 用）
    BackendKind kind = BackendKind::Video;
    bool hasPoster = false;
    bool hasBalanced = false;
    bool hasPowerSaver = false;
    uint64_t sourceBytes = 0;
};

struct WallPaperSettings {
    bool enabled = false;
    std::wstring activeId;                                  // 当前壁纸；空表示未选择
    VariantKind preferred = VariantKind::Original;
    bool pauseOnFullscreen = true;
    bool pauseOnLock = true;
    std::wstring libraryRoot;                               // 只读回显，实际归属由模块内部记录

    // 播放速度（0.25–4，1 为原速）。对 Video/AnimatedImage/ImageSequence 生效，
    // 对 Shader3D 缩放时间推进，对 Web 不适用。
    double speed = 1.0;
    // 音频全局开关，**默认关闭**；音量 0..1
    bool audioEnabled = false;
    float audioVolume = 1.0f;
};

} // namespace desktopsticker
