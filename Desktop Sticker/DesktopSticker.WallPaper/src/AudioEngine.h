#pragma once

// WASAPI 音频输出。自持线程，与画面解耦：
// 画面侧只需 SetSource / SetMuted / SetVolume / SetSpeed / ClockUs。
//
// 关键约束（规格 §4）：
//  - 暂停时**停止并释放设备**，不占着独占资源
//  - 有音频且未静音时才是主时钟（ClockUs 有效），否则画面沿用 QPC
//  - 无音频设备 / 设备被独占 → 降级为静音，绝不崩、绝不影响画面
//  - 变速通过重采样实现（同步变调），不做 time-stretch

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "AudioSource.h"
#include "desktopsticker/wallpaper/PcmConvert.h"

struct IAudioClient;
struct IAudioRenderClient;
struct IAudioClock;
struct IAudioEndpointVolume;
struct IMMDeviceEnumerator;

namespace desktopsticker::wallpaper {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // 设置当前音源（可为 nullptr = 静音）。引擎负责开关设备。
    void SetSource(std::unique_ptr<IAudioSource> source);

    void SetMuted(bool muted);
    void SetVolume(float volume);      // 0..1
    void SetSpeed(double speed);       // 与画面同步

    // 有效静音 = 用户没开声音（默认）**或系统已静音**。系统静音由音频线程轮询端点得到，
    // 因此系统音量静音/取消静音会实时反映到壁纸；用户没开声音时自然一直是静音。
    bool Muted() const { return muted_.load() || systemMuted_.load(); }
    bool SystemMuted() const { return systemMuted_.load(); }
    // 有效时返回播放位置（微秒）；无音源/静音/无设备返回 -1（调用方回落 QPC）
    int64_t ClockUs() const;

    // 暂停/恢复：暂停会停止并释放设备
    void SetPaused(bool paused);
    bool Paused() const { return paused_.load(); }

    const char* Status() const;        // 供日志/UI：设备状态简述
    int Channels() const { return channels_.load(); }
    int SampleRate() const { return sampleRate_.load(); }

private:
    void thread_main();
    bool open_device();                // 在音频线程上调用
    void close_device();               // 在音频线程上调用
    // 按需重开设备（音源更换 / 暂停恢复 / 设备失效）
    bool ensure_open_locked();
    void poll_system_mute();           // 在音频线程上调用

    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> muted_{true};    // 用户选择；规格：默认静音
    std::atomic<bool> systemMuted_{false};   // 系统端点静音状态（音频线程轮询刷新）
    std::atomic<float> volume_{1.0f};
    std::atomic<double> speed_{1.0};
    std::atomic<int64_t> clockUs_{-1};
    std::atomic<int> channels_{0};
    std::atomic<int> sampleRate_{0};

    std::mutex sourceMutex_;
    std::unique_ptr<IAudioSource> source_;
    bool sourceChanged_ = false;

    // 以下仅音频线程访问
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    IAudioClock* clock_ = nullptr;
    IAudioEndpointVolume* endpointVolume_ = nullptr;   // 只为读系统静音状态
    void* event_ = nullptr;            // HANDLE
    int deviceChannels_ = 0;
    int deviceRate_ = 0;
    bool deviceIsFloat_ = true;
    uint64_t clockFreq_ = 0;
    int64_t renderedFrames_ = 0;
    ULONGLONG lastMutePollMs_ = 0;
    PcmRateConverter converter_;
    std::vector<int16_t> mapped_;
    std::vector<int16_t> resampled_;
    std::vector<int16_t> pending_;      // 重采样后待写出的样本
    size_t pendingOffset_ = 0;
    std::string status_ = "未启动";
};

} // namespace desktopsticker::wallpaper
