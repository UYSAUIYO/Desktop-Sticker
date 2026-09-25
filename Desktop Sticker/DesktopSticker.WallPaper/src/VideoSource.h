#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace desktopsticker::wallpaper {

// 解码后统一输出 BGRA（与 DXGI_FORMAT_B8G8R8A8_UNORM 内存布局一致），
// 由呈现层按 cover 方式缩放铺满窗口。两条解码路径共用本接口，便于运行时切换。
class IVideoSource {
public:
    virtual ~IVideoSource() = default;

    virtual bool Open(const std::wstring& path) = 0;
    virtual void Close() = 0;
    // 取下一帧；false 表示当前无法出帧（含循环回卷的那一次，调用方下轮重试）
    virtual bool NextFrame(std::vector<uint8_t>& bgra, int& width, int& height) = 0;
    virtual double Fps() const = 0;
    // 上一帧的显示时长（毫秒）。GIF/WebP 可能逐帧不同；返回 0 表示请按 Fps() 推算
    virtual int FrameDurationMs() const { return 0; }
    // "Media Foundation" 或 "FFmpeg"，用于日志与状态显示
    virtual const char* Backend() const = 0;
};

// 解码输出上限：源分辨率高于屏幕时按显示尺寸解码，省下数倍 CPU 转换与搬运
// （见 DecodeTarget.h）。0 = 不限制，按源分辨率解码。
struct VideoSourceOptions {
    int maxWidth = 0;
    int maxHeight = 0;
    // 动图（GIF/WebP/APNG）优先用 FFmpeg：MF 的 WIC 源只给首帧，动不起来
    bool preferFfmpeg = false;
};

// 优先级：默认 MF 为主、MF 明确打不开该素材时才启用 FFmpeg 兜底；
// preferFfmpeg 为真时（动图）反过来，只有 FFmpeg 不可用才退回 MF。
// 两者都不可用时返回 nullptr，调用方保持最后一帧并记录状态。
std::unique_ptr<IVideoSource> open_video_source(const std::wstring& path,
                                                std::string* chosenBackend = nullptr,
                                                const VideoSourceOptions& options = {});

// FFmpeg 解码兜底是否可用（共享库是否加载成功）
bool ffmpeg_fallback_available();

// 每个进程调用一次；成功返回 true
bool video_subsystem_start();
void video_subsystem_stop();

} // namespace desktopsticker::wallpaper
