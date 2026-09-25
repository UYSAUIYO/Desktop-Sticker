#pragma once

// 本头文件被 EXE 侧与测试工程共同包含，只允许依赖标准库。
// 不得引入 windows.h / D3D / MF 依赖，否则会污染 EXE 与单测的编译环境。

#include <cstdint>
#include <string>

namespace desktopsticker {

// 播放档位：原画（MF→FFmpeg 兜底）或由 ffmpeg.exe 生成的性能副本
enum class VariantKind { Original, Balanced, PowerSaver };

struct WallPaperItem {
    std::wstring id;           // 库内唯一 id
    std::wstring name;         // 显示名，缺省为源文件主名
    std::wstring sourceFile;   // 相对 media\<id>\ 的源文件名
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
};

} // namespace desktopsticker
