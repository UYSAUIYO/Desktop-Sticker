#pragma once

// 有音轨的后端（Video / Web）把 PCM 交给 AudioEngine 播放。
// 解码细节（MF / FFmpeg）在 AudioSources.cpp 里，这里只定义契约。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace desktopsticker::wallpaper {

// 统一的 PCM 输出格式：16-bit 交错、源采样率、源声道数。
// 设备侧的格式转换与变速重采样由 AudioEngine 负责（PcmConvert.h）。
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    virtual bool HasAudio() const = 0;
    // 拉取最多 maxFrames 帧；返回实际产出的帧数（0 = 暂无数据/已到结尾）
    virtual size_t ReadFrames(int16_t* out, size_t maxFrames) = 0;
    virtual int Channels() const = 0;
    virtual int SampleRate() const = 0;
    // 循环播放：回到开头
    virtual bool SeekToStart() = 0;
    virtual void Close() = 0;
    virtual const char* Backend() const = 0;
};

// MF 为主（与画面一致），失败才用 FFmpeg。两者都不可用返回 nullptr。
std::unique_ptr<IAudioSource> open_audio_source(const std::wstring& path,
                                                std::string* chosenBackend = nullptr);

} // namespace desktopsticker::wallpaper
