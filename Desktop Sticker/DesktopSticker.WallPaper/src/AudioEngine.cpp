#include "pch.h"
#include "AudioEngine.h"

#include "Log.h"
#include "desktopsticker/wallpaper/ClockPolicy.h"
#include "desktopsticker/wallpaper/FrameAdvance.h"   // clamp_speed

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace desktopsticker::wallpaper {

namespace {

// 共享模式的缓冲区时长：100ms 足够稳，且不会让"暂停→恢复"的延迟太明显
constexpr REFERENCE_TIME kBufferDuration = 1000000; // 100ms（100ns 单位）
constexpr int kMaxFramesPerRead = 4096;
// 内层填充的迭代上限：防止音源异常时死循环
constexpr int kMaxFillIterations = 64;

float pcm_to_float(int16_t s) { return static_cast<float>(s) / 32768.0f; }

} // namespace

AudioEngine::AudioEngine() {
    thread_ = std::thread([this]() {
        SetThreadDescription(GetCurrentThread(), L"壁纸音频输出");
        thread_main();
    });
}

AudioEngine::~AudioEngine() {
    quit_.store(true);
    if (thread_.joinable()) thread_.join();
}

void AudioEngine::SetSource(std::unique_ptr<IAudioSource> source) {
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        source_ = std::move(source);
        sourceChanged_ = true;
    }
    converter_.Reset();
}

void AudioEngine::SetMuted(bool muted) { muted_.store(muted); }

void AudioEngine::SetVolume(float volume) {
    if (!(volume >= 0.0f)) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    volume_.store(volume);
}

void AudioEngine::SetSpeed(double speed) { speed_.store(clamp_speed(speed)); }

int64_t AudioEngine::ClockUs() const { return clockUs_.load(); }

void AudioEngine::SetPaused(bool paused) { paused_.store(paused); }

const char* AudioEngine::Status() const { return status_.c_str(); }

bool AudioEngine::open_device() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        status_ = "音频设备枚举器创建失败";
        return false;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        status_ = "没有默认播放设备";
        return false;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client_));
    if (FAILED(hr) || !client_) {
        client_ = nullptr;
        status_ = "音频客户端激活失败";
        return false;
    }

    WAVEFORMATEX* mix = nullptr;
    hr = client_->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        status_ = "取设备格式失败";
        close_device();
        return false;
    }
    deviceChannels_ = static_cast<int>(mix->nChannels);
    deviceRate_ = static_cast<int>(mix->nSamplesPerSec);

    // 设备是 float32 还是 16-bit PCM
    deviceIsFloat_ = true;
    if (mix->wFormatTag == WAVE_FORMAT_PCM) {
        deviceIsFloat_ = false;
    } else if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
               mix->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix);
        deviceIsFloat_ = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) {
        status_ = "事件创建失败";
        CoTaskMemFree(mix);
        close_device();
        return false;
    }

    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                             kBufferDuration, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) {
        status_ = "音频客户端初始化失败（设备可能被独占）";
        close_device();
        return false;
    }

    if (FAILED(client_->SetEventHandle(static_cast<HANDLE>(event_)))) {
        status_ = "绑定音频事件失败";
        close_device();
        return false;
    }

    if (FAILED(client_->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void**>(&render_))) ||
        !render_) {
        status_ = "取渲染客户端失败";
        close_device();
        return false;
    }

    // 时钟：拿不到就保持 -1，让画面回落 QPC —— 好过用错时钟
    ComPtr<IAudioClock> clock;
    if (SUCCEEDED(client_->GetService(IID_PPV_ARGS(&clock))) && clock) {
        UINT64 freq = 0;
        if (SUCCEEDED(clock->GetFrequency(&freq)) && freq > 0) {
            clock_ = clock.Detach();
            clockFreq_ = freq;
        }
    }

    // 系统静音：读一次初值，之后由主循环轮询。拿不到就当作"未静音"，
    // 反正用户自己的开关仍然管用，不会因此漏音。
    IAudioEndpointVolume* rawVolume = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&rawVolume))) &&
        rawVolume) {
        endpointVolume_ = rawVolume;
        poll_system_mute();
    } else {
        wp_log("audio: IAudioEndpointVolume unavailable; system mute will not be tracked");
    }

    renderedFrames_ = 0;
    clockUs_.store(-1);
    channels_.store(deviceChannels_);
    sampleRate_.store(deviceRate_);

    if (FAILED(client_->Start())) {
        status_ = "音频启动失败";
        close_device();
        return false;
    }

    status_ = "播放中";
    return true;
}

void AudioEngine::close_device() {
    if (client_) client_->Stop();
    if (render_) {
        render_->Release();
        render_ = nullptr;
    }
    if (clock_) {
        clock_->Release();
        clock_ = nullptr;
    }
    if (endpointVolume_) {
        endpointVolume_->Release();
        endpointVolume_ = nullptr;
    }
    systemMuted_.store(false);
    if (client_) {
        client_->Release();
        client_ = nullptr;
    }
    if (event_) {
        CloseHandle(static_cast<HANDLE>(event_));
        event_ = nullptr;
    }
    channels_.store(0);
    sampleRate_.store(0);
    clockUs_.store(-1);
    clockFreq_ = 0;
    pending_.clear();
    pendingOffset_ = 0;
}

void AudioEngine::poll_system_mute() {
    if (!endpointVolume_) return;
    BOOL muted = FALSE;
    if (FAILED(endpointVolume_->GetMute(&muted))) return;
    const bool now = (muted != FALSE);
    if (now == systemMuted_.load()) return;
    systemMuted_.store(now);
    wp_log(std::string("audio: system endpoint mute = ") + (now ? "1" : "0"));
}

bool AudioEngine::ensure_open_locked() {    bool changed = false;
    bool hasSource = false;
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        changed = sourceChanged_;
        sourceChanged_ = false;
        hasSource = (source_ != nullptr) && source_->HasAudio();
    }

    if (paused_.load() || !hasSource) {
        if (client_) {
            close_device();
        }
        status_ = paused_.load() ? "已暂停（设备已释放）" : "无音源";
        return false;
    }

    if (!client_ || changed) {
        if (client_) close_device();
        converter_.Reset();
        return open_device();
    }
    return true;
}

void AudioEngine::thread_main() {
    // COM 只在本线程初始化一次，与末尾的 CoUninitialize 配对
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOk = SUCCEEDED(comHr);

    while (!quit_.load()) {
        // 系统静音状态：端点对象归音频线程独占，定期轮询即可 ——
        // 用 RegisterControlChangeNotify 回调要额外管委托生命周期，不值。
        const ULONGLONG nowTick = GetTickCount64();
        if (nowTick - lastMutePollMs_ >= 250) {
            lastMutePollMs_ = nowTick;
            poll_system_mute();
        }

        if (!ensure_open_locked()) {
            Sleep(50);
            continue;
        }

        const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(event_), 20);
        if (quit_.load()) break;
        if (wait != WAIT_OBJECT_0) continue;

        UINT32 bufferFrames = 0;
        UINT32 padding = 0;
        if (FAILED(client_->GetBufferSize(&bufferFrames))) continue;
        if (FAILED(client_->GetCurrentPadding(&padding))) continue;
        const UINT32 available = bufferFrames - padding;
        if (available == 0) continue;

        BYTE* dst = nullptr;
        if (FAILED(render_->GetBuffer(available, &dst)) || !dst) continue;

        const int outCh = deviceChannels_ > 0 ? deviceChannels_ : 1;
        // 有效静音：用户没开声音 或 系统静音 —— 两者任一成立就写静音
        const float gain = Muted() ? 0.0f : volume_.load();
        const double speed = speed_.load();

        size_t written = 0;
        for (int iter = 0; iter < kMaxFillIterations && written < available; ++iter) {
            // 1) 先把上次重采样剩余的输出写掉
            if (pendingOffset_ < pending_.size()) {
                const size_t framesLeft =
                    (pending_.size() - pendingOffset_) / static_cast<size_t>(outCh);
                const size_t toWrite = std::min<size_t>(framesLeft, available - written);
                for (size_t f = 0; f < toWrite; ++f) {
                    for (int c = 0; c < outCh; ++c) {
                        const int16_t s = pending_[pendingOffset_ +
                                                   f * static_cast<size_t>(outCh) +
                                                   static_cast<size_t>(c)];
                        const float v = pcm_to_float(s) * gain;
                        const size_t idx = (written + f) * static_cast<size_t>(outCh) +
                                           static_cast<size_t>(c);
                        if (deviceIsFloat_) {
                            reinterpret_cast<float*>(dst)[idx] = v;
                        } else {
                            const float clipped = std::max(-1.0f, std::min(1.0f, v));
                            reinterpret_cast<int16_t*>(dst)[idx] =
                                static_cast<int16_t>(std::lround(clipped * 32767.0f));
                        }
                    }
                }
                pendingOffset_ += toWrite * static_cast<size_t>(outCh);
                written += toWrite;
                if (pendingOffset_ >= pending_.size()) {
                    pending_.clear();
                    pendingOffset_ = 0;
                }
                continue;
            }

            // 2) 没有存货就从音源拉一段，做声道映射与变速/采样率转换
            int srcChannels = 0;
            int srcRate = 0;
            size_t got = 0;
            {
                std::lock_guard<std::mutex> lock(sourceMutex_);
                if (!source_) break;
                srcChannels = source_->Channels();
                srcRate = source_->SampleRate();
                if (srcChannels <= 0 || srcRate <= 0) break;

                mapped_.resize(static_cast<size_t>(kMaxFramesPerRead) *
                               static_cast<size_t>(srcChannels));
                got = source_->ReadFrames(mapped_.data(), kMaxFramesPerRead);
                if (got == 0) {
                    // 循环播放：回开头再取一次；取不到就退出本轮填充
                    if (!source_->SeekToStart()) break;
                    continue;
                }
            }

            resampled_.clear();
            pcm_map_channels(mapped_.data(), got, srcChannels, outCh, resampled_);

            // ratio = 输出帧 / 输入帧 = deviceRate / (srcRate × speed)
            const double ratio = static_cast<double>(deviceRate_) /
                                 (static_cast<double>(srcRate) * speed);
            pending_.clear();
            pendingOffset_ = 0;
            converter_.Process(resampled_.data(), got, outCh, ratio, pending_);
            // pending_ 为空说明相位还没攒够一帧，下一轮继续
        }

        // 没填满的部分补静音：绝不能把未初始化的缓冲区交给设备
        if (written < available) {
            const size_t from = written * static_cast<size_t>(outCh);
            const size_t count = (available - written) * static_cast<size_t>(outCh);
            if (deviceIsFloat_) {
                float* p = reinterpret_cast<float*>(dst) + from;
                std::fill(p, p + count, 0.0f);
            } else {
                int16_t* p = reinterpret_cast<int16_t*>(dst) + from;
                std::fill(p, p + count, static_cast<int16_t>(0));
            }
        }

        render_->ReleaseBuffer(available, 0);
        renderedFrames_ += available;

        // 主时钟 = 设备真正在播的位置（不是我们写入的位置）
        if (clock_ && clockFreq_ > 0) {
            UINT64 pos = 0;
            UINT64 qpc = 0;
            if (SUCCEEDED(clock_->GetPosition(&pos, &qpc))) {
                clockUs_.store(static_cast<int64_t>(pos * 1000000ULL / clockFreq_));
            } else {
                clockUs_.store(-1);
            }
        } else {
            clockUs_.store(-1);
        }
    }

    close_device();
    if (comOk) CoUninitialize();
}

} // namespace desktopsticker::wallpaper
