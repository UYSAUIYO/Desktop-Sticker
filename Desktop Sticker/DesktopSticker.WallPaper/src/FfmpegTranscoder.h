#pragma once

#include <string>

#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

// 通过未经修改的 ffmpeg.exe 生成性能副本与抽帧缩略图。
// 串行队列：同一时间最多一个转码进程，避免多路转码抢占 CPU。
class FfmpegTranscoder {
public:
    // ffmpegDir 为负载目录（含 ffmpeg.exe）
    explicit FfmpegTranscoder(std::wstring ffmpegDir);

    bool Available() const;

    // 生成性能副本。output 必须落在库内该条目的 variants\ 下；绝不指向源文件。
    // timeoutMs 到点则 TerminateProcess。
    bool RunTranscode(const std::wstring& input,
                      const std::wstring& output,
                      VariantKind kind,
                      unsigned timeoutMs = 10u * 60u * 1000u);

    // 抽一帧存为 PNG（用于 poster.png）。scaleWidth<=0 表示不缩放。
    bool RunThumbnail(const std::wstring& input,
                      const std::wstring& outputPng,
                      int scaleWidth = 512,
                      unsigned timeoutMs = 60u * 1000u);

private:
    bool run(const std::wstring& exe, const std::vector<std::wstring>& args, unsigned timeoutMs);
    // 固定的 LGPL 构建没有 x264，按可用性挑一个 H.264 编码器（结果缓存）
    const std::wstring& pick_encoder();
    std::wstring exePath_;
    std::wstring encoder_;
    bool encoderProbed_ = false;
    mutable std::mutex queueMutex_; // 串行化队列
};

} // namespace desktopsticker::wallpaper
