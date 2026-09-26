#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace desktopsticker::wallpaper {

// 一帧解码结果，**二选一**：
//   · CPU 路径：BGRA 写进调用方给的缓冲（零拷贝，软件解码/兜底路径）
//   · GPU 路径：NV12 的 D3D11 纹理（硬件解码直接把帧写进显存，不再落回内存）
//
// GPU 纹理的**生命周期只到下一次 NextFrame 为止** —— 它是解码器的输出池，
// 下次读样本就可能被回收，所以调用方必须在那之前画完，绝不能留着跨帧用。
struct VideoFrame {
    std::vector<uint8_t>* pixels = nullptr;   // CPU：调用方提供的缓冲
    ID3D11Texture2D* texture = nullptr;       // GPU：NV12 纹理
    uint32_t subresource = 0;                 // 纹理数组里的切片
    int width = 0;
    int height = 0;
    // 媒体时间戳（100ns 单位，-1 = 该路径还不提供）。节拍与"落后即丢帧"要靠它，
    // 用自己那条 1/fps 网格只能保证 tick 均匀，保证不了"这一帧真的是新内容"。
    int64_t pts100ns = -1;

    bool on_gpu() const { return texture != nullptr; }
};

// 两条解码路径共用本接口，便于运行时切换。
class IVideoSource {
public:
    virtual ~IVideoSource() = default;

    virtual bool Open(const std::wstring& path) = 0;
    virtual void Close() = 0;
    // 取下一帧；false 表示当前无法出帧（含循环回卷的那一次，调用方下轮重试）
    virtual bool NextFrame(VideoFrame& out) = 0;
    virtual double Fps() const = 0;
    // 上一帧的显示时长（毫秒）。GIF/WebP 可能逐帧不同；返回 0 表示请按 Fps() 推算
    virtual int FrameDurationMs() const { return 0; }
    // "Media Foundation (D3D11 硬解)" / "Media Foundation" / "FFmpeg"，用于日志与状态显示
    virtual const char* Backend() const = 0;
    // 是否走 GPU 纹理路径（呈现侧据此选 D3D11 直绘而不是 D2D 上传）
    virtual bool UsesGpu() const { return false; }
};

// 解码输出上限：源分辨率高于屏幕时按显示尺寸解码，省下数倍 CPU 转换与搬运
// （见 DecodeTarget.h）。0 = 不限制，按源分辨率解码。
struct VideoSourceOptions {
    int maxWidth = 0;
    int maxHeight = 0;
    // 动图（GIF/WebP/APNG）优先用 FFmpeg：MF 的 WIC 源只给首帧，动不起来
    bool preferFfmpeg = false;
    // 非空则尝试**硬件解码**（D3D11 设备管理器 + NV12 纹理输出）；任何一步失败都自动
    // 回落到软件 RGB32 路径，不会因此打不开素材
    ID3D11Device* d3dDevice = nullptr;
    // 让 FFmpeg 走 NVIDIA 硬解（NVDEC / CUDA）：解码在显存里完成，再搬回内存做色彩转换。
    // 设备建不起来或解码器不支持时自动退回软件解码。
    bool useNvdec = false;
};

// 优先级：MF 为主（有 D3D 设备时优先试硬解），MF 明确打不开该素材时才启用 FFmpeg 兜底。
// 动图偏好 FFmpeg。两者都不可用时返回 nullptr，调用方保持最后一帧并记录状态。
std::unique_ptr<IVideoSource> open_video_source(const std::wstring& path,
                                                std::string* chosenBackend = nullptr,
                                                const VideoSourceOptions& options = {});

// FFmpeg 解码兜底是否可用（共享库是否加载成功）
bool ffmpeg_fallback_available();

// 每个进程调用一次；成功返回 true
bool video_subsystem_start();
void video_subsystem_stop();

} // namespace desktopsticker::wallpaper
