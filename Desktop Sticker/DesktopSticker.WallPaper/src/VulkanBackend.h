#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "WallpaperBackend.h"

// ④ 阶段 0 spike：把 Vulkan 头文件做成**可选**依赖（规格 §8.1：按固定 commit 拉头文件、
// __has_include 门控）。拿不到头文件时本后端整体退化为"不可用"，其余后端与主程序照常编译。
#if defined(__has_include)
#  if __has_include(<vulkan/vulkan.hpp>)
#    define DSTK_HAVE_VULKAN 1
#  endif
#endif

namespace desktopsticker::wallpaper {

// 3D / 着色器后端（④）：自持 Vulkan 交换链，在**壁纸窗口自己的 HWND** 上直接呈现
// （vk::raii::SurfaceKHR，不做 Vulkan 与 DComp 的 external memory 互操作）。
//
// 这一版只是阶段 0 的 spike：一个内置全屏着色器铺满桌面。用它先回答两个问题 ——
//   a) Vulkan 交换链在"嵌入 WorkerW 的壁纸窗口"上到底能不能呈现（③ Web 就栽在类似位置）
//   b) Vulkan 呈现与 D3dContext 已有的 DComp 目标抢同一个 HWND 时谁遮谁
// 通过了再叠加模型（gltf/glb/obj）与粒子预设。
class VulkanBackend final : public IWallpaperBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    bool Open(const BackendRequest& request, const BackendContext& ctx) override;
    void Close() override;

    bool SelfPresenting() const override { return true; }
    bool ProduceFrame(VideoFrame&) override { return false; }
    void Tick() override;

    void SetPaused(bool paused) override;
    // 调速在这里的含义是"着色器时间的推进速率"（规格 §11）
    void SetSpeed(double speed) override;
    double TargetFps() const override { return 60.0; }
    // 让渲染循环的 fps diag 也能统计着色器帧数（自呈现型只走 Tick，没有 ProduceFrame）
    uint32_t FrameSerial() const override { return static_cast<uint32_t>(frames_); }

    const char* Name() const override { return "3D / 着色器"; }
    BackendKind Kind() const override { return BackendKind::Shader3D; }
    const char* LastError() const { return lastError_.c_str(); }

private:
    struct Impl;                 // 在 .cpp 里，避免把 vulkan.hpp 漏进本头文件
    std::unique_ptr<Impl> impl_;
    std::string lastError_;
    bool paused_ = false;
    uint64_t frames_ = 0;
};

// Vulkan 是否可用（loader 能加载 + 至少一个物理设备）。结果缓存，可在 UI 线程调用。
bool vulkan_available();

} // namespace desktopsticker::wallpaper
