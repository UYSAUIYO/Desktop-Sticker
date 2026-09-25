#pragma once

// PCM 处理纯函数：声道映射 + 采样率/速度转换。
// 不碰任何设备，可单测。
//
// 采样格式统一为 16-bit 交错（int16_t），设备侧再由 AudioEngine 转成 WASAPI 需要的
// float32；统一在 16-bit 上做声道与速率处理，代码最少且足够。

#include <cstddef>
#include <cstdint>
#include <vector>

namespace desktopsticker::wallpaper {

// 声道映射：in 为 frames 帧、inChannels 声道的交错 PCM。
// 单声道→多声道按复制填充；多声道→单声道取前两声道平均；相同则直通。
inline void pcm_map_channels(const int16_t* in, size_t frames, int inChannels, int outChannels,
                             std::vector<int16_t>& out) {
    if (!in || frames == 0 || inChannels <= 0 || outChannels <= 0) return;

    for (size_t f = 0; f < frames; ++f) {
        const int16_t* src = in + f * static_cast<size_t>(inChannels);
        if (inChannels == outChannels) {
            for (int c = 0; c < outChannels; ++c) out.push_back(src[c]);
        } else if (inChannels == 1) {
            for (int c = 0; c < outChannels; ++c) out.push_back(src[0]);
        } else if (outChannels == 1) {
            // 取前两声道平均，其它声道忽略（壁纸配乐无需专业下混）
            const int32_t mix = (static_cast<int32_t>(src[0]) + static_cast<int32_t>(src[1])) / 2;
            out.push_back(static_cast<int16_t>(mix));
        } else {
            // 其它情况：能取几个取几个，不足补 0；多余声道截断
            for (int c = 0; c < outChannels; ++c) {
                out.push_back(c < inChannels ? src[c] : static_cast<int16_t>(0));
            }
        }
    }
}

// 采样率 / 速度转换：分数累加器 + 最近邻取样。
// ratio = 输出帧数 / 输入帧数（>1 变慢、<1 变快）。跨块保留小数相位，保证连续。
//
// 说明：最近邻在非 1× 时会有轻微混叠失真，这是**有意取舍** —— 壁纸配乐级别足够，
// 且避免引入重采样依赖。将来若要提质，只需把本函数内换成立性插值。
struct PcmRateConverter {
    double pos = 0.0;

    void Reset() { pos = 0.0; }

    void Process(const int16_t* in, size_t frames, int channels, double ratio,
                 std::vector<int16_t>& out) {
        if (!in || frames == 0 || channels <= 0 || !(ratio > 0.0)) return;

        const double total = static_cast<double>(frames);
        while (pos < total) {
            const size_t i = static_cast<size_t>(pos);
            if (i >= frames) break;
            const int16_t* src = in + i * static_cast<size_t>(channels);
            for (int c = 0; c < channels; ++c) out.push_back(src[c]);
            pos += ratio;
        }
        pos -= total;                 // 只保留小数相位，跨块连续
        if (pos < 0.0) pos = 0.0;
    }
};

} // namespace desktopsticker::wallpaper
