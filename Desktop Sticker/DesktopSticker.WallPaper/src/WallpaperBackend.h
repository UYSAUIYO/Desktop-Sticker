#pragma once

// 壁纸后端契约。四类来源里：
//   产帧型（Video / AnimatedImage / ImageSequence）→ 由 D3dContext 的 D3D11+DComp 上屏
//   自呈现型（Web / Shader3D）→ 自己拥有窗口内容，D3dContext 必须让位（见 PresentationArbiter）
//
// 后端全部在**渲染线程**上 Open/Close/Tick/ProduceFrame，绝不跨线程使用。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AudioEngine.h"
#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

struct BackendContext {
    HWND window = nullptr;            // 已挂到桌面层的壁纸窗口
    int width = 0;
    int height = 0;
    ID3D11Device* d3dDevice = nullptr; // 仅产帧型需要
    std::wstring exeDir;               // 内置资源所在
    std::wstring libraryRoot;          // 壁纸库根
    AudioEngine* audio = nullptr;      // 可为 nullptr（无音频设备时）
};

struct BackendRequest {
    BackendKind kind = BackendKind::Video;
    std::wstring sourcePath;   // 视频/动图 = 文件；序列/网页/着色器 = 目录
    std::wstring paramsPath;   // 可选：Shader3D 的参数 JSON
    double speed = 1.0;
};

class IWallpaperBackend {
public:
    virtual ~IWallpaperBackend() = default;

    // 在渲染线程上打开。失败返回 false（调用方据此保持当前画面，不得变黑）
    virtual bool Open(const BackendRequest& request, const BackendContext& ctx) = 0;
    virtual void Close() = 0;

    // true = 自己呈现，调用方不调用 ProduceFrame
    virtual bool SelfPresenting() const = 0;

    // 产帧型：产出一帧 BGRA（stride = w*4）。返回 false 表示本轮无新帧
    virtual bool ProduceFrame(std::vector<uint8_t>& bgra, int& w, int& h) = 0;

    // 自呈现型：每轮调用一次，由后端决定是否重绘
    virtual void Tick() {}

    // 暂停：自呈现型必须真正停（Web 挂起 / Vulkan 停提交）
    virtual void SetPaused(bool) = 0;

    virtual void SetSpeed(double speed) = 0;

    // 期望的循环节拍（帧/秒）。调速由后端内部按策略消费帧实现，
    // 所以这里返回的是**源**帧率，不乘速度。
    virtual double TargetFps() const { return 30.0; }

    virtual const char* Name() const = 0;
    virtual BackendKind Kind() const = 0;
};

// 按类型创建后端；不支持的类型返回 nullptr（模块据此降级）
std::unique_ptr<IWallpaperBackend> create_backend(BackendKind kind);

// 该后端类型在当前构建中是否可用（例如缺 WebView2 运行时）
bool backend_available(BackendKind kind);

} // namespace desktopsticker::wallpaper
