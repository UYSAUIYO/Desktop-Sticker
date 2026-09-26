#include "pch.h"
#include "VulkanBackend.h"

#include "Log.h"
#include "Utf8.h"
#include "VulkanBuiltinShaders.h"
#include "desktopsticker/wallpaper/FrameAdvance.h"   // clamp_speed
#include "desktopsticker/wallpaper/MatMath.h"
#include "desktopsticker/wallpaper/Polyhedron.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>

#if defined(DSTK_HAVE_VULKAN)

// 动态入口解析：从系统 vulkan-1.dll 取全部函数指针，不链接导入库、不依赖 Vulkan SDK
// （规格 §8.1）。下面三个宏必须在 include 之前定义，且与 vcxproj 里的定义保持一致。
#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#  define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
// 不定义它就没有 vk::Win32SurfaceCreateInfoKHR（平台类型是按需生成的）
#ifndef VK_USE_PLATFORM_WIN32_KHR
#  define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <limits>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace desktopsticker::wallpaper {

namespace {

int64_t qpc_us() {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000LL / freq;
}

// 用 Win32 读环境变量：getenv 在这个工程里按 C4996 处理（不可用）
bool env_flag_set(const wchar_t* name) {
    wchar_t buf[8]{};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    return n > 0 && n < std::size(buf) && buf[0] == L'1';
}

// 上传给 GPU 的顶点布局：与 mesh.vert 的 location 0/1/2 对应
struct GpuVertex {
    float position[3];
    float normal[3];
    float color[3];
};

// 与参考实现（Three.js 那份 HTML）同一套配色
const Vec3 kPalette[] = {
    { 0.231, 0.510, 0.965 },   // #3b82f6
    { 0.133, 0.827, 0.933 },   // #22d3ee
    { 0.388, 0.400, 0.949 },   // #6366f1
    { 0.055, 0.647, 0.910 },   // #0ea5e9
    { 0.545, 0.361, 0.965 },   // #8b5cf6
    { 0.078, 0.722, 0.651 },   // #14b8a6
};

enum class SceneMode { Fullscreen, Model };

// 选场景：`scene.json` 里显式写了就听它的；否则"有 .frag 就当全屏着色器，
// 没有就当内置模型"。这样两种内容都能被 classify_directory 认出来并导入。
SceneMode pick_scene(const std::wstring& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path sceneFile = fs::path(dir) / L"scene.json";
    if (fs::is_regular_file(sceneFile, ec)) {
        try {
            std::ifstream in(sceneFile, std::ios::binary);
            const auto j = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(in),
                                                            std::istreambuf_iterator<char>()));
            if (j.contains("scene") && j["scene"].is_string()) {
                const std::string s = j["scene"].get<std::string>();
                if (s == "model") return SceneMode::Model;
                if (s == "fullscreen") return SceneMode::Fullscreen;
            }
        } catch (const std::exception& e) {
            wp_log(std::string("vulkan: scene.json parse failed: ") + e.what());
        }
    }

    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == L".frag") {
            return SceneMode::Fullscreen;
        }
    }
    return SceneMode::Model;
}

} // namespace

// ---------------------------------------------------------------------------

struct VulkanBackend::Impl {
    ~Impl() { destroy(); }

    // 访问违例兜底。Vulkan 有一类失败是 try/catch 抓不到的：动态分发器里的空指针、
    // 驱动的越界访问 —— 第一版就撞上了（进程无声退出，事件日志只剩 "unknown 模块 偏移 0"）。
    // 壁纸后端的约定是"不可用就降级"，所以初始化整段用 SEH 兜住。
    // 一个函数里只能有一种异常处理方式（C2713），所以 C++ 的 try/catch 留在调用方 Open()，
    // 本函数只做 SEH 转发。
    static bool init_seh(Impl* self, HWND hwnd, int width, int height, const std::wstring& dir) {
        __try {
            return self->init(hwnd, width, height, dir);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ---- 生命周期 ----
    bool init(HWND hwnd, int width, int height, const std::wstring& sourceDir);
    void destroy();
    void wait_idle();

    // ---- 分步构建 ----
    bool create_instance();
    bool create_surface(HWND hwnd);
    bool pick_physical_device();
    bool create_device();
    bool build_swapchain(int width, int height);
    bool build_fullscreen_pipeline();
    bool build_mesh_scene();
    bool build_mesh_pipeline();
    bool build_glass_pipeline();
    bool build_wire_pipeline();
    bool build_reflect_pipeline();
    bool build_stars_pipeline();
    bool build_sync_and_commands();
    bool rebuild_swapchain(int width, int height);

    uint32_t find_memory_type(uint32_t typeBits, vk::MemoryPropertyFlags want) const;
    bool create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::raii::Buffer& buffer,
                       vk::raii::DeviceMemory& memory);

    bool draw(float timeSeconds);
    void draw_fullscreen(vk::CommandBuffer cmd, const vk::Extent2D& extent);
    void draw_stars(vk::CommandBuffer cmd, const vk::Extent2D& extent);
    void draw_model(vk::CommandBuffer cmd, const vk::Extent2D& extent, float timeSecondsValue);
    bool submit_and_present();

    // ---- 顺序敏感：raii 成员按声明**逆序**析构，依赖别人的必须后声明 ----
    vk::raii::Context context;                 // 无依赖
    vk::raii::Instance instance{ nullptr };
    vk::raii::SurfaceKHR surface{ nullptr };
    vk::raii::PhysicalDevice physical{ nullptr };
    vk::raii::Device device{ nullptr };
    vk::raii::Queue queue{ nullptr };

    uint32_t queueFamily = 0;
    vk::Format depthFormat = vk::Format::eD32Sfloat;

    // 交换链相关：分辨率变化时要整块重建，所以单独一层，重建 = 换一个对象
    struct Swap {
        vk::raii::SwapchainKHR swapchain{ nullptr };
        vk::Extent2D extent{};
        std::vector<vk::raii::ImageView> views;
        vk::raii::Image depthImage{ nullptr };
        vk::raii::DeviceMemory depthMemory{ nullptr };
        vk::raii::ImageView depthView{ nullptr };
        std::vector<vk::raii::Framebuffer> framebuffers;
        std::vector<vk::raii::CommandBuffer> cmdBuffers;
        vk::raii::Semaphore imageAvailable{ nullptr };
        vk::raii::Semaphore renderFinished{ nullptr };
        vk::raii::Fence inFlight{ nullptr };
    };
    std::unique_ptr<Swap> swap;
    vk::Format format = vk::Format::eB8G8R8A8Unorm;   // Win32 表面上必然可呈现，所以固定不变

    // 与交换链无关、只依赖 format 的东西：建一次就够
    vk::raii::RenderPass renderPass{ nullptr };
    vk::raii::CommandPool cmdPool{ nullptr };

    // 全屏着色器管线（push constant）
    vk::raii::PipelineLayout fsLayout{ nullptr };
    vk::raii::Pipeline fsPipeline{ nullptr };

    // 模型场景（uniform buffer + 顶点缓冲 + 多管线：实体/玻璃壳/倒影/线框笼/星空）
    SceneMode mode = SceneMode::Fullscreen;
    vk::raii::DescriptorSetLayout descLayout{ nullptr };
    vk::raii::PipelineLayout meshLayout{ nullptr };
    vk::raii::Pipeline meshPipeline{ nullptr };
    vk::raii::Pipeline glassPipeline{ nullptr };
    vk::raii::Pipeline wirePipeline{ nullptr };
    vk::raii::Pipeline reflectPipeline{ nullptr };
    vk::raii::Pipeline starsPipeline{ nullptr };
    vk::raii::DescriptorPool descPool{ nullptr };
    vk::raii::DescriptorSet descSet{ nullptr };
    vk::raii::Buffer vertexBuffer{ nullptr };
    vk::raii::DeviceMemory vertexMemory{ nullptr };
    vk::raii::Buffer glassBuffer{ nullptr };
    vk::raii::DeviceMemory glassMemory{ nullptr };
    vk::raii::Buffer wireBuffer{ nullptr };
    vk::raii::DeviceMemory wireMemory{ nullptr };
    vk::raii::Buffer uniformBuffer{ nullptr };
    vk::raii::DeviceMemory uniformMemory{ nullptr };
    void* uniformMapped = nullptr;
    uint32_t vertexCount = 0;
    uint32_t glassVertexCount = 0;
    uint32_t wireVertexCount = 0;

    HWND hwnd = nullptr;
    uint32_t imageIndex = 0;
    double timeSeconds = 0.0;
    int64_t lastTickUs = 0;
    double speed = 1.0;
    int swapWidth = 0;
    int swapHeight = 0;
    std::string lastError;

    // 帧率诊断（与渲染循环的 fps diag 同样的用途：卡顿时能直接看出时间花在哪一段）
    int64_t diagStartUs = 0;
    int diagTicks = 0;
    int64_t diagAcquireUs = 0;
    int64_t diagPresentUs = 0;
};

// ---------------------------------------------------------------------------

uint32_t VulkanBackend::Impl::find_memory_type(uint32_t typeBits,
                                               vk::MemoryPropertyFlags want) const {
    const auto props = physical.getMemoryProperties();
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return 0;
}

bool VulkanBackend::Impl::create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                        vk::raii::Buffer& buffer,
                                        vk::raii::DeviceMemory& memory) {
    buffer = vk::raii::Buffer(device, vk::BufferCreateInfo({}, size, usage,
                                                          vk::SharingMode::eExclusive));
    const auto req = buffer.getMemoryRequirements();
    // 一次性上传的小缓冲直接用 host-visible，省掉暂存缓冲与拷贝
    memory = vk::raii::DeviceMemory(
        device, vk::MemoryAllocateInfo(req.size,
                                       find_memory_type(req.memoryTypeBits,
                                                        vk::MemoryPropertyFlagBits::eHostVisible |
                                                            vk::MemoryPropertyFlagBits::eHostCoherent)));
    buffer.bindMemory(*memory, 0);
    return true;
}

bool VulkanBackend::Impl::init(HWND wnd, int width, int height, const std::wstring& sourceDir) {
    hwnd = wnd;
    mode = pick_scene(sourceDir);

    // 先把入口解析出来：DynamicLoader 自己负责 LoadLibrary("vulkan-1.dll")
    vk::detail::DynamicLoader loader;
    const PFN_vkGetInstanceProcAddr gipa =
        loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (!gipa) {
        lastError = "vulkan-1.dll 不可用";
        return false;
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(gipa);

    if (!create_instance()) return false;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    if (!create_surface(hwnd)) return false;
    if (!pick_physical_device()) return false;
    if (!create_device()) return false;
    if (!build_swapchain(width, height)) return false;
    if (!build_fullscreen_pipeline()) return false;
    if (mode == SceneMode::Model) {
        if (!build_mesh_scene()) return false;
        if (!build_mesh_pipeline()) return false;
        if (!build_glass_pipeline()) return false;
        if (!build_wire_pipeline()) return false;
        if (!build_reflect_pipeline()) return false;
        if (!build_stars_pipeline()) return false;
    }
    if (!build_sync_and_commands()) return false;

    timeSeconds = 0.0;
    lastTickUs = qpc_us();
    return true;
}

bool VulkanBackend::Impl::create_instance() {
    const uint32_t loaderVersion = context.enumerateInstanceVersion();
    if (loaderVersion < VK_MAKE_API_VERSION(0, 1, 1, 0)) {
        lastError = "Vulkan loader 低于 1.1";
        return false;
    }

    // 请求 1.1 而不是 loader 的最高版本：壁纸不需要新特性，降版本能少踩驱动的坑
    const vk::ApplicationInfo appInfo("DesktopSticker", 1, "WallPaper", 1, VK_API_VERSION_1_1);

    // 表面扩展**必须**在实例上启用：不启用的话 vkCreateWin32SurfaceKHR 在动态分发器里
    // 就是空指针，调用时直接崩在地址 0（第一版就是这么挂的：进程无声退出，事件日志
    // 只有 "unknown 模块 偏移 0"）。所以先确认它们真的存在，再启用。
    const char* instanceExts[] = { VK_KHR_SURFACE_EXTENSION_NAME,
                                  VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    const auto haveExts = context.enumerateInstanceExtensionProperties();
    for (const char* want : instanceExts) {
        bool found = false;
        for (const auto& e : haveExts) {
            if (std::strcmp(e.extensionName, want) == 0) { found = true; break; }
        }
        if (!found) {
            lastError = std::string("缺少实例扩展 ") + want;
            return false;
        }
    }

    // 校验层只在显式设了 DSTK_VULKAN_VALIDATION=1 且系统真的装了时才开，
    // 绝不因为它缺失就启动失败
    const char* layerName = "VK_LAYER_KHRONOS_validation";
    bool useLayer = false;
    if (env_flag_set(L"DSTK_VULKAN_VALIDATION")) {
        for (const auto& l : context.enumerateInstanceLayerProperties()) {
            if (std::strcmp(l.layerName, layerName) == 0) { useLayer = true; break; }
        }
        if (!useLayer) wp_log("vulkan: validation layer requested but not installed; skipping");
    }

    if (useLayer) {
        instance = vk::raii::Instance(
            context, vk::InstanceCreateInfo(vk::InstanceCreateFlags{}, &appInfo, 1, &layerName,
                                            static_cast<uint32_t>(std::size(instanceExts)),
                                            instanceExts));
    } else {
        instance = vk::raii::Instance(
            context, vk::InstanceCreateInfo(vk::InstanceCreateFlags{}, &appInfo, 0, nullptr,
                                            static_cast<uint32_t>(std::size(instanceExts)),
                                            instanceExts));
    }
    return *instance != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::create_surface(HWND target) {
    const vk::Win32SurfaceCreateInfoKHR info(vk::Win32SurfaceCreateFlagsKHR{},
                                             GetModuleHandleW(nullptr), target);
    surface = vk::raii::SurfaceKHR(instance, info);
    return *surface != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::pick_physical_device() {
    // 优先独显；虚拟显示器（本机有 OrayIddDriver / GameViewer）一般不暴露 Vulkan，
    // 但真出现了也要能筛掉 —— 判据是"有图形队列 + 能向本窗口呈现"
    const auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        lastError = "没有 Vulkan 物理设备";
        return false;
    }

    int best = -1;
    vk::PhysicalDeviceType bestType = vk::PhysicalDeviceType::eCpu;
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto fp = devices[i].getProperties();
        const auto families = devices[i].getQueueFamilyProperties();
        bool usable = false;
        for (uint32_t f = 0; f < families.size(); ++f) {
            if (!(families[f].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
            if (devices[i].getSurfaceSupportKHR(f, *surface) != VK_TRUE) continue;
            usable = true;
            break;
        }
        if (!usable) {
            wp_log(std::string("vulkan: skip device '") + std::string(fp.deviceName.data()) +
                   "' (no graphics+present queue)");
            continue;
        }
        const bool discrete = fp.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
        const bool bestDiscrete = bestType == vk::PhysicalDeviceType::eDiscreteGpu;
        if (best < 0 || (discrete && !bestDiscrete)) {
            best = static_cast<int>(i);
            bestType = fp.deviceType;
        }
    }
    if (best < 0) {
        lastError = "没有能向壁纸窗口呈现的 Vulkan 设备";
        return false;
    }

    physical = devices[static_cast<size_t>(best)];
    const auto families = physical.getQueueFamilyProperties();
    for (uint32_t f = 0; f < families.size(); ++f) {
        if ((families[f].queueFlags & vk::QueueFlagBits::eGraphics) &&
            physical.getSurfaceSupportKHR(f, *surface) == VK_TRUE) {
            queueFamily = f;
            break;
        }
    }
    wp_log(std::string("vulkan: using device '") +
           std::string(physical.getProperties().deviceName.data()) + "'");
    return true;
}

bool VulkanBackend::Impl::create_device() {
    const float priority = 1.0f;
    const vk::DeviceQueueCreateInfo qci({}, queueFamily, 1, &priority);
    const char* exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    device = vk::raii::Device(
        physical, vk::DeviceCreateInfo(vk::DeviceCreateFlags{}, 1, &qci, 0, nullptr, 1, exts));
    queue = vk::raii::Queue(device, queueFamily, 0);

    // 设备级入口（vkCreateSwapchainKHR / vkQueueSubmit / vkCmd* …）也在动态分发器里，
    // 初始化过 device 之后才非空 —— 漏了这一步同样会崩在地址 0
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
    return *device != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::build_swapchain(int width, int height) {
    const auto caps = physical.getSurfaceCapabilitiesKHR(*surface);
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        swapWidth = static_cast<int>(caps.currentExtent.width);
        swapHeight = static_cast<int>(caps.currentExtent.height);
    } else {
        const int w = std::max(width, static_cast<int>(caps.minImageExtent.width));
        const int h = std::max(height, static_cast<int>(caps.minImageExtent.height));
        swapWidth = std::min(w, static_cast<int>(caps.maxImageExtent.width));
        swapHeight = std::min(h, static_cast<int>(caps.maxImageExtent.height));
    }

    vk::SurfaceFormatKHR chosen{};
    bool haveFormat = false;
    for (const auto& f : physical.getSurfaceFormatsKHR(*surface)) {
        if (f.format == format) { chosen = f; haveFormat = true; break; }
    }
    if (!haveFormat) {
        lastError = "表面不支持 B8G8R8A8";
        return false;
    }

    // FIFO 一定被支持；它是 vsync 锁定的，壁纸不需要 MAILBOX 那种冲高帧率
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    // 深度格式：模型要用。D32 支持就用，否则退 D16
    const auto depthFeatures = physical.getFormatProperties(vk::Format::eD32Sfloat)
                                   .optimalTilingFeatures;
    depthFormat = (depthFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
                      ? vk::Format::eD32Sfloat
                      : vk::Format::eD16Unorm;

    auto s = std::make_unique<Swap>();
    s->extent = vk::Extent2D(static_cast<uint32_t>(swapWidth), static_cast<uint32_t>(swapHeight));
    s->swapchain = vk::raii::SwapchainKHR(
        device, vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR{}, *surface, imageCount,
                                           format, chosen.colorSpace, s->extent, 1,
                                           vk::ImageUsageFlagBits::eColorAttachment,
                                           vk::SharingMode::eExclusive, 0, nullptr,
                                           caps.currentTransform,
                                           vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                           vk::PresentModeKHR::eFifo, VK_TRUE, nullptr));

    const auto images = s->swapchain.getImages();
    s->views.reserve(images.size());
    for (const auto& img : images) {
        s->views.emplace_back(device, vk::ImageViewCreateInfo(
            {}, img, vk::ImageViewType::e2D, format,
            vk::ComponentMapping{}, vk::ImageSubresourceRange(
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
    }

    // 深度附件
    s->depthImage = vk::raii::Image(
        device, vk::ImageCreateInfo({}, vk::ImageType::e2D, depthFormat,
                                    vk::Extent3D(s->extent.width, s->extent.height, 1), 1, 1,
                                    vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                                    vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                    vk::SharingMode::eExclusive));
    {
        const auto req = s->depthImage.getMemoryRequirements();
        s->depthMemory = vk::raii::DeviceMemory(
            device, vk::MemoryAllocateInfo(req.size,
                                           find_memory_type(req.memoryTypeBits,
                                                            vk::MemoryPropertyFlagBits::eDeviceLocal)));
        s->depthImage.bindMemory(*s->depthMemory, 0);
    }
    s->depthView = vk::raii::ImageView(
        device, vk::ImageViewCreateInfo({}, *s->depthImage, vk::ImageViewType::e2D, depthFormat,
                                        vk::ComponentMapping{},
                                        vk::ImageSubresourceRange(
                                            vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)));

    // 渲染通道：颜色 + 深度。两条管线共用它（全屏着色器不使用深度，但共用没问题）
    if (*renderPass == VK_NULL_HANDLE) {
        const vk::AttachmentDescription attachments[2] = {
            vk::AttachmentDescription({}, format, vk::SampleCountFlagBits::e1,
                                      vk::AttachmentLoadOp::eClear,
                                      vk::AttachmentStoreOp::eStore,
                                      vk::AttachmentLoadOp::eDontCare,
                                      vk::AttachmentStoreOp::eDontCare,
                                      vk::ImageLayout::eUndefined,
                                      vk::ImageLayout::ePresentSrcKHR),
            vk::AttachmentDescription({}, depthFormat, vk::SampleCountFlagBits::e1,
                                      vk::AttachmentLoadOp::eClear,
                                      vk::AttachmentStoreOp::eDontCare,
                                      vk::AttachmentLoadOp::eDontCare,
                                      vk::AttachmentStoreOp::eDontCare,
                                      vk::ImageLayout::eUndefined,
                                      vk::ImageLayout::eDepthStencilAttachmentOptimal),
        };
        const vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);
        const vk::AttachmentReference depthRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        const vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, 0, nullptr,
                                             1, &colorRef, nullptr, &depthRef);
        renderPass = vk::raii::RenderPass(
            device, vk::RenderPassCreateInfo({}, attachments, subpass));
    }

    s->framebuffers.reserve(s->views.size());
    for (const auto& v : s->views) {
        const vk::ImageView fbAttachments[2] = { *v, *s->depthView };
        s->framebuffers.emplace_back(device, vk::FramebufferCreateInfo(
            {}, *renderPass, 2, fbAttachments, s->extent.width, s->extent.height, 1));
    }

    imageIndex = 0;
    swap = std::move(s);
    wp_log("vulkan: swapchain " + std::to_string(swapWidth) + "x" + std::to_string(swapHeight) +
           ", " + std::to_string(images.size()) + " images, depth=" +
           (depthFormat == vk::Format::eD32Sfloat ? "D32" : "D16"));
    return true;
}

bool VulkanBackend::Impl::build_fullscreen_pipeline() {
    const auto vert = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kFullscreenVertSpv), kFullscreenVertSpv));
    const auto frag = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kDefaultFragSpv), kDefaultFragSpv));

    const vk::PipelineShaderStageCreateInfo stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main" },
    };

    const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eVertex |
                                              vk::ShaderStageFlagBits::eFragment,
                                          0, sizeof(ShaderPushConstants));
    fsLayout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, 0, nullptr, 1,
                                                                            &pushRange));

    const vk::PipelineVertexInputStateCreateInfo vertexInput;   // 不用顶点缓冲
    const vk::PipelineInputAssemblyStateCreateInfo assembly(
        {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    const vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
    const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    const vk::PipelineColorBlendAttachmentState blendAttachment(
        VK_FALSE, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1,
                                                     &blendAttachment);
    const vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport,
                                              vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);

    fsPipeline = vk::raii::Pipeline(
        device, nullptr,
        vk::GraphicsPipelineCreateInfo({}, 2, stages, &vertexInput, &assembly, nullptr, &viewport,
                                       &raster, &multisample, nullptr, &blend, &dynamic, *fsLayout,
                                       *renderPass, 0));
    return *fsPipeline != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::build_mesh_scene() {
    // ---- 几何：倒角 32 面体。面板/倒角条/顶帽在纯函数层生成（水密性有单测），
    //      另出玻璃壳（原凸包放大）与外层线框笼（原始边去重）。
    const std::vector<Vec3> palette(std::begin(kPalette), std::end(kPalette));
    const BevelMesh bevel = build_beveled_truncated_icosahedron(palette, 2.0, 0.16);
    vertexCount = static_cast<uint32_t>(bevel.solid.vertices.size());
    if (vertexCount == 0) {
        lastError = "内置模型没有生成任何三角形";
        return false;
    }

    auto upload = [&](const std::vector<GpuVertex>& verts, vk::raii::Buffer& buffer,
                      vk::raii::DeviceMemory& memory) -> bool {
        const vk::DeviceSize bytes = sizeof(GpuVertex) * verts.size();
        if (!create_buffer(bytes, vk::BufferUsageFlagBits::eVertexBuffer, buffer, memory)) {
            return false;
        }
        void* mapped = memory.mapMemory(0, bytes);
        std::memcpy(mapped, verts.data(), static_cast<size_t>(bytes));
        memory.unmapMemory();
        return true;
    };

    std::vector<GpuVertex> solidVerts(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        const MeshVertex& src = bevel.solid.vertices[i];
        GpuVertex& dst = solidVerts[i];
        dst.position[0] = static_cast<float>(src.position.x);
        dst.position[1] = static_cast<float>(src.position.y);
        dst.position[2] = static_cast<float>(src.position.z);
        dst.normal[0] = static_cast<float>(src.normal.x);
        dst.normal[1] = static_cast<float>(src.normal.y);
        dst.normal[2] = static_cast<float>(src.normal.z);
        dst.color[0] = static_cast<float>(src.color.x);
        dst.color[1] = static_cast<float>(src.color.y);
        dst.color[2] = static_cast<float>(src.color.z);
    }
    if (!upload(solidVerts, vertexBuffer, vertexMemory)) return false;

    // 玻璃壳：原凸包放大 7%，着色器按菲涅尔给半透明
    const MeshData glass = build_truncated_icosahedron({}, 2.14);
    glassVertexCount = static_cast<uint32_t>(glass.vertices.size());
    std::vector<GpuVertex> glassVerts(glassVertexCount);
    for (uint32_t i = 0; i < glassVertexCount; ++i) {
        const MeshVertex& src = glass.vertices[i];
        GpuVertex& dst = glassVerts[i];
        dst.position[0] = static_cast<float>(src.position.x);
        dst.position[1] = static_cast<float>(src.position.y);
        dst.position[2] = static_cast<float>(src.position.z);
        dst.normal[0] = static_cast<float>(src.normal.x);
        dst.normal[1] = static_cast<float>(src.normal.y);
        dst.normal[2] = static_cast<float>(src.normal.z);
    }
    if (!upload(glassVerts, glassBuffer, glassMemory)) return false;

    // 线框笼：原始边的线段列表（线段拓扑绘制）
    wireVertexCount = static_cast<uint32_t>(bevel.wire.size());
    std::vector<GpuVertex> wireVerts(wireVertexCount);
    for (uint32_t i = 0; i < wireVertexCount; ++i) {
        const MeshVertex& src = bevel.wire[i];
        GpuVertex& dst = wireVerts[i];
        dst.position[0] = static_cast<float>(src.position.x);
        dst.position[1] = static_cast<float>(src.position.y);
        dst.position[2] = static_cast<float>(src.position.z);
        dst.color[0] = static_cast<float>(src.color.x);
        dst.color[1] = static_cast<float>(src.color.y);
        dst.color[2] = static_cast<float>(src.color.z);
    }
    if (!upload(wireVerts, wireBuffer, wireMemory)) return false;

    const vk::DeviceSize ubytes = sizeof(SceneUniforms);
    if (!create_buffer(ubytes, vk::BufferUsageFlagBits::eUniformBuffer, uniformBuffer,
                       uniformMemory)) {
        return false;
    }
    uniformMapped = uniformMemory.mapMemory(0, ubytes);

    // 描述符布局必须在建管线之前就位（管线布局引用它）
    const vk::DescriptorSetLayoutBinding binding(
        0, vk::DescriptorType::eUniformBuffer, 1,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
    descLayout = vk::raii::DescriptorSetLayout(
        device, vk::DescriptorSetLayoutCreateInfo({}, binding));

    if (!build_mesh_pipeline()) return false;

    // 注意 DescriptorPoolCreateInfo 的构造函数是 (flags, maxSets, poolSizeCount, pPoolSizes)
    const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, 1);
    descPool = vk::raii::DescriptorPool(
        device, vk::DescriptorPoolCreateInfo({}, 1, 1, &poolSize));
    descSet = std::move(device.allocateDescriptorSets(
        vk::DescriptorSetAllocateInfo(*descPool, 1, &*descLayout))[0]);

    const vk::DescriptorBufferInfo bufferInfo(*uniformBuffer, 0, ubytes);
    device.updateDescriptorSets(
        vk::WriteDescriptorSet(*descSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr,
                               &bufferInfo),
        nullptr);

    wp_log("vulkan: built-in model ready, panels=" + std::to_string(bevel.panelCount) +
           " strips=" + std::to_string(bevel.stripCount) + " caps=" +
           std::to_string(bevel.capCount) + " (" + std::to_string(vertexCount / 3) +
           " triangles), wires=" + std::to_string(bevel.edgeCount));
    return true;
}

bool VulkanBackend::Impl::build_mesh_pipeline() {
    const auto vert = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kMeshVertSpv), kMeshVertSpv));
    const auto frag = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kBevelFragSpv), kBevelFragSpv));

    const vk::PipelineShaderStageCreateInfo stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main" },
    };

    // 模型矩阵与参数走 push constant：一帧里四条通道各不相同
    const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eVertex |
                                              vk::ShaderStageFlagBits::eFragment,
                                          0, sizeof(ModelPush));
    meshLayout = vk::raii::PipelineLayout(
        device, vk::PipelineLayoutCreateInfo({}, 1, &*descLayout, 1, &pushRange));

    const vk::VertexInputBindingDescription vb(0, sizeof(GpuVertex),
                                               vk::VertexInputRate::eVertex);
    const vk::VertexInputAttributeDescription attrs[] = {
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, position) },
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, normal) },
        { 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, color) },
    };
    const vk::PipelineVertexInputStateCreateInfo vertexInput({}, vb, attrs);
    const vk::PipelineInputAssemblyStateCreateInfo assembly(
        {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    // 背面剔除：凸包的环是从外面看逆时针，所以正面是 CCW
    const vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
    const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    // 关掉深度写入会让背面挡住正面，开着才是实体
    const vk::PipelineDepthStencilStateCreateInfo depthState({}, VK_TRUE, VK_TRUE,
                                                            vk::CompareOp::eLess);
    const vk::PipelineColorBlendAttachmentState blendAttachment(
        VK_FALSE, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1,
                                                     &blendAttachment);
    const vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport,
                                              vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);

    meshPipeline = vk::raii::Pipeline(
        device, nullptr,
        vk::GraphicsPipelineCreateInfo({}, 2, stages, &vertexInput, &assembly, nullptr, &viewport,
                                       &raster, &multisample, &depthState, &blend, &dynamic,
                                       *meshLayout, *renderPass, 0));
    return *meshPipeline != VK_NULL_HANDLE;
}

// 玻璃壳/倒影：开混合、只测深度不写；线框笼：线段拓扑。
// 三者共享实体管线的顶点输入与 push constant 布局。
bool VulkanBackend::Impl::build_glass_pipeline() {
    const auto vert = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kMeshVertSpv), kMeshVertSpv));
    const auto frag = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kGlassFragSpv), kGlassFragSpv));
    const vk::PipelineShaderStageCreateInfo stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main" },
    };

    // 复用成员 meshLayout（descLayout + ModelPush push range，与本地临时布局等价）：
    // 局部 raiii 布局在函数返回时销毁，而管线必须终身持有有效布局——否则首次 bind 闪退
    const vk::PushConstantRange meshPushRange(vk::ShaderStageFlagBits::eVertex |
                                                  vk::ShaderStageFlagBits::eFragment,
                                              0, sizeof(ModelPush));
    (void)meshPushRange;

    const vk::VertexInputBindingDescription vb(0, sizeof(GpuVertex),
                                               vk::VertexInputRate::eVertex);
    const vk::VertexInputAttributeDescription attrs[] = {
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, position) },
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, normal) },
        { 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, color) },
    };
    const vk::PipelineVertexInputStateCreateInfo vertexInput({}, vb, attrs);
    const vk::PipelineInputAssemblyStateCreateInfo assembly(
        {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    const vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
    const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    const vk::PipelineDepthStencilStateCreateInfo depthState({}, VK_TRUE, VK_FALSE,
                                                            vk::CompareOp::eLessOrEqual);
    const vk::PipelineColorBlendAttachmentState blendAttachment(
        VK_TRUE, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha,
        vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1,
                                                     &blendAttachment);
    const vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport,
                                              vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);

    glassPipeline = vk::raii::Pipeline(
        device, nullptr,
        vk::GraphicsPipelineCreateInfo({}, 2, stages, &vertexInput, &assembly, nullptr, &viewport,
                                       &raster, &multisample, &depthState, &blend, &dynamic,
                                       *meshLayout, *renderPass, 0));
    return *glassPipeline != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::build_wire_pipeline() {
    const auto vert = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kMeshVertSpv), kMeshVertSpv));
    const auto frag = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kWireFragSpv), kWireFragSpv));
    const vk::PipelineShaderStageCreateInfo stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main" },
    };

    // 复用成员 meshLayout（descLayout + ModelPush push range，与本地临时布局等价）：
    // 局部 raiii 布局在函数返回时销毁，而管线必须终身持有有效布局——否则首次 bind 闪退
    const vk::PushConstantRange meshPushRange(vk::ShaderStageFlagBits::eVertex |
                                                  vk::ShaderStageFlagBits::eFragment,
                                              0, sizeof(ModelPush));
    (void)meshPushRange;

    const vk::VertexInputBindingDescription vb(0, sizeof(GpuVertex),
                                               vk::VertexInputRate::eVertex);
    const vk::VertexInputAttributeDescription attrs[] = {
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, position) },
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, normal) },
        { 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, color) },
    };
    const vk::PipelineVertexInputStateCreateInfo vertexInput({}, vb, attrs);
    const vk::PipelineInputAssemblyStateCreateInfo assembly(
        {}, vk::PrimitiveTopology::eLineList, VK_FALSE);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    const vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
    const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    const vk::PipelineDepthStencilStateCreateInfo depthState({}, VK_TRUE, VK_FALSE,
                                                            vk::CompareOp::eLessOrEqual);
    const vk::PipelineColorBlendAttachmentState blendAttachment(
        VK_TRUE, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha,
        vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1,
                                                     &blendAttachment);
    const vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport,
                                              vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);

    wirePipeline = vk::raii::Pipeline(
        device, nullptr,
        vk::GraphicsPipelineCreateInfo({}, 2, stages, &vertexInput, &assembly, nullptr, &viewport,
                                       &raster, &multisample, &depthState, &blend, &dynamic,
                                       *meshLayout, *renderPass, 0));
    return *wirePipeline != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::build_reflect_pipeline() {
    const auto vert = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kMeshVertSpv), kMeshVertSpv));
    const auto frag = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kBevelFragSpv), kBevelFragSpv));
    const vk::PipelineShaderStageCreateInfo stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main" },
    };

    // 复用成员 meshLayout（descLayout + ModelPush push range，与本地临时布局等价）：
    // 局部 raiii 布局在函数返回时销毁，而管线必须终身持有有效布局——否则首次 bind 闪退
    const vk::PushConstantRange meshPushRange(vk::ShaderStageFlagBits::eVertex |
                                                  vk::ShaderStageFlagBits::eFragment,
                                              0, sizeof(ModelPush));
    (void)meshPushRange;

    const vk::VertexInputBindingDescription vb(0, sizeof(GpuVertex),
                                               vk::VertexInputRate::eVertex);
    const vk::VertexInputAttributeDescription attrs[] = {
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, position) },
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, normal) },
        { 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(GpuVertex, color) },
    };
    const vk::PipelineVertexInputStateCreateInfo vertexInput({}, vb, attrs);
    const vk::PipelineInputAssemblyStateCreateInfo assembly(
        {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    // 镜像矩阵翻转了环绕方向，索性不剔除
    const vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
    const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    const vk::PipelineDepthStencilStateCreateInfo depthState({}, VK_TRUE, VK_FALSE,
                                                            vk::CompareOp::eLessOrEqual);
    const vk::PipelineColorBlendAttachmentState blendAttachment(
        VK_TRUE, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha,
        vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1,
                                                     &blendAttachment);
    const vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport,
                                              vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);

    reflectPipeline = vk::raii::Pipeline(
        device, nullptr,
        vk::GraphicsPipelineCreateInfo({}, 2, stages, &vertexInput, &assembly, nullptr, &viewport,
                                       &raster, &multisample, &depthState, &blend, &dynamic,
                                       *meshLayout, *renderPass, 0));
    return *reflectPipeline != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::build_stars_pipeline() {
    // 全屏星空打底：与全屏等离子共用 push constant 布局与顶点输入（无顶点缓冲）
    const auto vert = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kFullscreenVertSpv), kFullscreenVertSpv));
    const auto frag = device.createShaderModule(
        vk::ShaderModuleCreateInfo({}, sizeof(kStarsFragSpv), kStarsFragSpv));

    const vk::PipelineShaderStageCreateInfo stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main" },
    };

    const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eVertex |
                                              vk::ShaderStageFlagBits::eFragment,
                                          0, sizeof(ShaderPushConstants));
    vk::raii::PipelineLayout layout(
        device, vk::PipelineLayoutCreateInfo({}, 0, nullptr, 1, &pushRange));

    const vk::PipelineVertexInputStateCreateInfo vertexInput;   // 不用顶点缓冲
    const vk::PipelineInputAssemblyStateCreateInfo assembly(
        {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);
    const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    const vk::PipelineRasterizationStateCreateInfo raster(
        {}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
    const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    const vk::PipelineColorBlendAttachmentState blendAttachment(
        VK_FALSE, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    const vk::PipelineColorBlendStateCreateInfo blend({}, VK_FALSE, vk::LogicOp::eCopy, 1,
                                                     &blendAttachment);
    const vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport,
                                              vk::DynamicState::eScissor };
    const vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);

    starsPipeline = vk::raii::Pipeline(
        device, nullptr,
        vk::GraphicsPipelineCreateInfo({}, 2, stages, &vertexInput, &assembly, nullptr, &viewport,
                                       &raster, &multisample, nullptr, &blend, &dynamic,
                                       *fsLayout, *renderPass, 0));
    return *starsPipeline != VK_NULL_HANDLE;
}

bool VulkanBackend::Impl::build_sync_and_commands() {
    cmdPool = vk::raii::CommandPool(
        device, vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                          queueFamily));

    auto& s = *swap;
    s.imageAvailable = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{});
    s.renderFinished = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{});
    s.inFlight = vk::raii::Fence(device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));

    const vk::CommandBufferAllocateInfo alloc(*cmdPool, vk::CommandBufferLevel::ePrimary,
                                             static_cast<uint32_t>(s.framebuffers.size()));
    s.cmdBuffers = device.allocateCommandBuffers(alloc);
    return true;
}

bool VulkanBackend::Impl::rebuild_swapchain(int width, int height) {
    if (*device == VK_NULL_HANDLE) return false;
    wait_idle();
    swap.reset();          // 先放掉依赖旧交换链的一切，再重建
    return build_swapchain(width, height) && build_sync_and_commands();
}

void VulkanBackend::Impl::draw_fullscreen(vk::CommandBuffer cmd, const vk::Extent2D& extent) {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *fsPipeline);
    const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width),
                                static_cast<float>(extent.height), 0.0f, 1.0f);
    cmd.setViewport(0, { viewport });
    cmd.setScissor(0, { vk::Rect2D({ 0, 0 }, extent) });

    ShaderPushConstants pc{};
    pc.iTime[0] = static_cast<float>(timeSeconds);
    pc.iResolution[0] = static_cast<float>(extent.width);
    pc.iResolution[1] = static_cast<float>(extent.height);
    cmd.pushConstants<ShaderPushConstants>(*fsLayout,
                                           vk::ShaderStageFlagBits::eVertex |
                                               vk::ShaderStageFlagBits::eFragment,
                                           0, pc);
    cmd.draw(3, 1, 0, 0);
}

void VulkanBackend::Impl::draw_stars(vk::CommandBuffer cmd, const vk::Extent2D& extent) {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *starsPipeline);
    const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width),
                                static_cast<float>(extent.height), 0.0f, 1.0f);
    cmd.setViewport(0, { viewport });
    cmd.setScissor(0, { vk::Rect2D({ 0, 0 }, extent) });

    ShaderPushConstants pc{};
    pc.iTime[0] = static_cast<float>(timeSeconds);
    pc.iResolution[0] = static_cast<float>(extent.width);
    pc.iResolution[1] = static_cast<float>(extent.height);
    cmd.pushConstants<ShaderPushConstants>(*fsLayout,
                                           vk::ShaderStageFlagBits::eVertex |
                                               vk::ShaderStageFlagBits::eFragment,
                                           0, pc);
    cmd.draw(3, 1, 0, 0);
}

void VulkanBackend::Impl::draw_model(vk::CommandBuffer cmd, const vk::Extent2D& extent,
                                     float timeSecondsValue) {
    // 内置环绕相机 + 模型自转：壁纸不吃输入（窗口是 HTTRANSPARENT），
    // 所以参考实现里的"拖拽旋转/滚轮缩放"换成自动环绕。
    const double t = static_cast<double>(timeSecondsValue);
    const double yaw = t * 0.45;
    const double tilt = std::sin(t * 0.23) * 0.21;
    // 距离要够远才装得下：fovY=0.62 时，距离 d 处的可见半高是 d*tan(0.31)，
    // 模型半径 2（玻璃壳 2.14、线框笼 2.6）→ d 至少要到 ~9
    const float distance = 11.5f;
    const Vec3 eye{ 0.0, distance * 0.40, distance * 0.94 };
    const Vec3 target{ 0.0, 0.0, 0.0 };

    const double aspect = extent.height > 0
                              ? static_cast<double>(extent.width) / extent.height
                              : 1.0;
    const Mat4 view = look_at(eye, target, { 0.0, 1.0, 0.0 });
    const Mat4 proj = perspective_vulkan(0.62, aspect, 0.1, 100.0);
    const Mat4 viewProj = multiply(proj, view);
    const Mat4 rot = multiply(rotation_y(yaw), rotation_x(tilt));

    SceneUniforms u{};
    std::memcpy(u.viewProj, viewProj.m, sizeof(u.viewProj));
    const Vec3 light = normalize({ 0.45, 0.80, 0.40 });
    u.lightDir[0] = static_cast<float>(light.x);
    u.lightDir[1] = static_cast<float>(light.y);
    u.lightDir[2] = static_cast<float>(light.z);
    u.eyePos[0] = static_cast<float>(eye.x);
    u.eyePos[1] = static_cast<float>(eye.y);
    u.eyePos[2] = static_cast<float>(eye.z);
    u.misc[0] = timeSecondsValue;
    if (uniformMapped) std::memcpy(uniformMapped, &u, sizeof(u));

    const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width),
                                static_cast<float>(extent.height), 0.0f, 1.0f);
    cmd.setViewport(0, { viewport });
    cmd.setScissor(0, { vk::Rect2D({ 0, 0 }, extent) });

    // 场景序列：星空 → 倒影（镜像+渐隐）→ 线框笼（反向旋转）→ 倒角实体 → 玻璃壳。
    // 透明通道统一"只测深度不写"，避免透明物互相剔除。

    // 1) 星空打底
    draw_stars(cmd, extent);

    const vk::DeviceSize offset = 0;

    // 2) 倒影：绕 y=-2 的地面镜像，缝隙发光强度减半，随沉入深度渐隐（bevel.frag）
    {
        Mat4 mirror{};
        mirror.m[0] = 1.0;
        mirror.m[5] = -1.0;
        mirror.m[10] = 1.0;
        mirror.m[13] = -4.0;   // 列主序：第 3 列的平移分量
        mirror.m[15] = 1.0;
        ModelPush mp{};
        const Mat4 refl = multiply(mirror, rot);
        std::memcpy(mp.model, refl.m, sizeof(mp.model));
        mp.params[0] = 0.30f;
        mp.params[1] = 0.55f;
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *reflectPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *meshLayout, 0, *descSet,
                               nullptr);
        cmd.bindVertexBuffers(0, *vertexBuffer, offset);
        cmd.pushConstants<ModelPush>(*meshLayout,
                                     vk::ShaderStageFlagBits::eVertex |
                                         vk::ShaderStageFlagBits::eFragment,
                                     0, mp);
        cmd.draw(vertexCount, 1, 0, 0);
    }

    // 3) 线框笼：反向旋转、放大到 2.64，罩住整个实体
    {
        ModelPush mp{};
        const Mat4 cage = multiply(scale_uniform(1.32),
                                   multiply(rotation_y(-yaw * 1.6), rotation_x(-tilt * 1.5)));
        std::memcpy(mp.model, cage.m, sizeof(mp.model));
        mp.params[0] = 0.9f;
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *wirePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *meshLayout, 0, *descSet,
                               nullptr);
        cmd.bindVertexBuffers(0, *wireBuffer, offset);
        cmd.pushConstants<ModelPush>(*meshLayout,
                                     vk::ShaderStageFlagBits::eVertex |
                                         vk::ShaderStageFlagBits::eFragment,
                                     0, mp);
        cmd.draw(wireVertexCount, 1, 0, 0);
    }

    // 4) 倒角实体（不透明，写深度）
    {
        ModelPush mp{};
        std::memcpy(mp.model, rot.m, sizeof(mp.model));
        mp.params[0] = 1.0f;
        mp.params[1] = 1.0f;
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *meshPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *meshLayout, 0, *descSet,
                               nullptr);
        cmd.bindVertexBuffers(0, *vertexBuffer, offset);
        cmd.pushConstants<ModelPush>(*meshLayout,
                                     vk::ShaderStageFlagBits::eVertex |
                                         vk::ShaderStageFlagBits::eFragment,
                                     0, mp);
        cmd.draw(vertexCount, 1, 0, 0);
    }

    // 5) 玻璃壳：原凸包放大 7%，菲涅尔半透明
    {
        ModelPush mp{};
        const Mat4 shell = multiply(scale_uniform(1.07), rot);
        std::memcpy(mp.model, shell.m, sizeof(mp.model));
        mp.params[0] = 1.0f;
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *glassPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *meshLayout, 0, *descSet,
                               nullptr);
        cmd.bindVertexBuffers(0, *glassBuffer, offset);
        cmd.pushConstants<ModelPush>(*meshLayout,
                                     vk::ShaderStageFlagBits::eVertex |
                                         vk::ShaderStageFlagBits::eFragment,
                                     0, mp);
        cmd.draw(glassVertexCount, 1, 0, 0);
    }
}

bool VulkanBackend::Impl::draw(float timeSecondsValue) {
    auto& s = *swap;

    const int64_t t0 = qpc_us();
    device.waitForFences(*s.inFlight, VK_TRUE, UINT64_MAX);
    const auto acquired = s.swapchain.acquireNextImage(UINT64_MAX, *s.imageAvailable, nullptr);
    diagAcquireUs += qpc_us() - t0;
    const vk::Result acqResult = acquired.first;
    if (acqResult == vk::Result::eErrorOutOfDateKHR ||
        acqResult == vk::Result::eSuboptimalKHR) {
        return false;      // 调用方据此重建交换链
    }
    imageIndex = acquired.second;
    if (imageIndex >= s.cmdBuffers.size() || imageIndex >= s.framebuffers.size()) return false;

    device.resetFences(*s.inFlight);
    vk::CommandBuffer cmd = *s.cmdBuffers[imageIndex];
    cmd.begin(vk::CommandBufferBeginInfo{});

    const vk::ClearColorValue bg(std::array<float, 4>{ 0.027f, 0.043f, 0.071f, 1.0f });  // #070b12
    const vk::ClearValue clears[2] = {
        vk::ClearValue(bg),
        vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0)),
    };
    cmd.beginRenderPass(vk::RenderPassBeginInfo(*renderPass, *s.framebuffers[imageIndex],
                                                vk::Rect2D({ 0, 0 }, s.extent), 2, clears),
                        vk::SubpassContents::eInline);
    if (mode == SceneMode::Model) {
        draw_model(cmd, s.extent, timeSecondsValue);
    } else {
        draw_fullscreen(cmd, s.extent);
    }
    cmd.endRenderPass();
    cmd.end();
    return true;
}

bool VulkanBackend::Impl::submit_and_present() {
    auto& s = *swap;

    const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    const vk::Semaphore waitSemaphores[] = { *s.imageAvailable };
    const vk::Semaphore signalSemaphores[] = { *s.renderFinished };
    const vk::CommandBuffer cmds[] = { *s.cmdBuffers[imageIndex] };
    queue.submit(vk::SubmitInfo(1, waitSemaphores, &waitStage, 1, cmds, 1, signalSemaphores),
                 *s.inFlight);

    const vk::SwapchainKHR swapchains[] = { *s.swapchain };
    const uint32_t indices[] = { imageIndex };
    const int64_t t1 = qpc_us();
    try {
        queue.presentKHR(vk::PresentInfoKHR(1, signalSemaphores, 1, swapchains, indices));
    } catch (const vk::OutOfDateKHRError&) {
        diagPresentUs += qpc_us() - t1;
        return false;      // 交换链过期：重建后下一拍再出图
    }
    diagPresentUs += qpc_us() - t1;
    return true;
}

void VulkanBackend::Impl::wait_idle() {
    if (*device != VK_NULL_HANDLE) device.waitIdle();
}

void VulkanBackend::Impl::destroy() {
    // 先等 GPU 停下再拆缓冲，否则可能拆到还在被引用的资源
    wait_idle();
    if (uniformMapped && *uniformMemory != VK_NULL_HANDLE) {
        uniformMemory.unmapMemory();
        uniformMapped = nullptr;
    }
    descSet = nullptr;
    descPool = nullptr;
    descLayout = nullptr;
    meshPipeline = nullptr;
    glassPipeline = nullptr;
    wirePipeline = nullptr;
    reflectPipeline = nullptr;
    starsPipeline = nullptr;
    meshLayout = nullptr;
    glassBuffer = nullptr;
    glassMemory = nullptr;
    wireBuffer = nullptr;
    wireMemory = nullptr;
    vertexBuffer = nullptr;
    vertexMemory = nullptr;
    glassVertexCount = wireVertexCount = 0;

    swap.reset();
    fsPipeline = nullptr;
    fsLayout = nullptr;
    cmdPool = nullptr;
    renderPass = nullptr;
    queue = nullptr;
    device = nullptr;
    physical = nullptr;
    surface = nullptr;
    instance = nullptr;
    hwnd = nullptr;
    swapWidth = swapHeight = 0;
    vertexCount = 0;
}

// ---------------------------------------------------------------------------

VulkanBackend::VulkanBackend() : impl_(std::make_unique<Impl>()) {}

VulkanBackend::~VulkanBackend() { Close(); }

bool VulkanBackend::Open(const BackendRequest& request, const BackendContext& ctx) {
    Close();
    if (!ctx.window) {
        lastError_ = "no host window";
        return false;
    }
    if (!ctx.dcompDevice) {
        // D3dContext 还没建起来时没法让位，Vulkan 与它会抢同一个 HWND
        wp_log("vulkan backend: DComp host missing; refusing to open");
        lastError_ = "no DComp host";
        return false;
    }

    try {
        if (!Impl::init_seh(impl_.get(), ctx.window, ctx.width, ctx.height,
                            request.sourcePath)) {
            lastError_ = impl_->lastError.empty() ? "Vulkan init failed" : impl_->lastError;
            wp_log("vulkan backend: " + lastError_);
            impl_->destroy();
            return false;
        }
    } catch (const std::exception& e) {
        lastError_ = e.what();
        wp_log(std::string("vulkan backend: exception: ") + e.what());
        impl_->destroy();
        return false;
    }

    impl_->speed = clamp_speed(request.speed);
    wp_log(std::string("vulkan backend opened: scene=") +
           (impl_->mode == SceneMode::Model ? "built-in model" : "fullscreen shader"));
    return true;
}

void VulkanBackend::Close() {
    if (!impl_) return;
    try {
        impl_->destroy();
    } catch (...) {
        // 析构路径不允许抛出去
    }
}

void VulkanBackend::Tick() {
    // 暂停就彻底停提交（规格 §3.1：自呈现型必须真正停），画面留在最后一帧
    if (!impl_ || !impl_->swap || paused_) return;

    // 着色器时间按速度推进（规格 §11：速度缩放 iTime 的推进速率）
    const int64_t now = qpc_us();
    if (impl_->lastTickUs > 0 && now > impl_->lastTickUs) {
        impl_->timeSeconds +=
            static_cast<double>(now - impl_->lastTickUs) / 1e6 * impl_->speed;
    }
    impl_->lastTickUs = now;

    try {
        if (!impl_->draw(static_cast<float>(impl_->timeSeconds))) {
            RECT rc{};
            int w = impl_->swapWidth, h = impl_->swapHeight;
            if (impl_->hwnd && GetClientRect(impl_->hwnd, &rc)) {
                w = rc.right - rc.left;
                h = rc.bottom - rc.top;
            }
            if (!impl_->rebuild_swapchain(w, h)) {
                lastError_ = impl_->lastError;
                wp_log("vulkan backend: swapchain rebuild failed: " + lastError_);
                return;
            }
            return;
        }
        if (!impl_->submit_and_present()) {
            RECT rc{};
            if (impl_->hwnd && GetClientRect(impl_->hwnd, &rc)) {
                impl_->rebuild_swapchain(rc.right - rc.left, rc.bottom - rc.top);
            }
        }
        ++frames_;

        // 每 2 秒一行：出帧率 + acquire/present 各自耗时（卡顿时先看这里）
        const int64_t now2 = qpc_us();
        if (impl_->diagStartUs == 0) impl_->diagStartUs = now2;
        ++impl_->diagTicks;
        if (now2 - impl_->diagStartUs >= 2000000) {
            const double secs = static_cast<double>(now2 - impl_->diagStartUs) / 1e6;
            wp_log("vulkan diag: ticks=" + std::to_string(impl_->diagTicks / secs) + "/s acquire=" +
                   std::to_string(impl_->diagAcquireUs / impl_->diagTicks) + "us present=" +
                   std::to_string(impl_->diagPresentUs / impl_->diagTicks) + "us");
            impl_->diagStartUs = now2;
            impl_->diagTicks = 0;
            impl_->diagAcquireUs = impl_->diagPresentUs = 0;
        }
    } catch (const std::exception& e) {
        lastError_ = e.what();
        wp_log(std::string("vulkan backend: present exception: ") + e.what());
    }
}

void VulkanBackend::SetPaused(bool paused) {
    if (!impl_) return;
    paused_ = paused;
    if (!paused) impl_->lastTickUs = qpc_us();   // 恢复时别把暂停时长算进 iTime
}

void VulkanBackend::SetSpeed(double speed) {
    if (!impl_) return;
    impl_->speed = clamp_speed(speed);
}

bool vulkan_available() {
    static const bool available = [] {
        try {
            vk::raii::Context ctx;
            if (ctx.enumerateInstanceVersion() < VK_MAKE_API_VERSION(0, 1, 1, 0)) return false;
            vk::detail::DynamicLoader loader;
            if (!loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr")) {
                return false;
            }
            const vk::ApplicationInfo appInfo("DesktopSticker", 1, "WallPaper", 1,
                                              VK_API_VERSION_1_1);
            const vk::raii::Instance instance(
                ctx, vk::InstanceCreateInfo(vk::InstanceCreateFlags{}, &appInfo));
            return !instance.enumeratePhysicalDevices().empty();
        } catch (...) {
            return false;
        }
    }();
    return available;
}

} // namespace desktopsticker::wallpaper

#else  // !DSTK_HAVE_VULKAN

// 没有 Vulkan 头文件时：④ 整体不可用，其余后端与主程序照常编译（规格 §13）
namespace desktopsticker::wallpaper {

// 必须有完整定义：unique_ptr<Impl> 的析构要在这里实例化
struct VulkanBackend::Impl {};

VulkanBackend::VulkanBackend() = default;
VulkanBackend::~VulkanBackend() = default;
bool VulkanBackend::Open(const BackendRequest&, const BackendContext&) {
    lastError_ = "built without Vulkan headers";
    wp_log("vulkan backend: this build has no Vulkan headers; ④ unavailable");
    return false;
}
void VulkanBackend::Close() {}
void VulkanBackend::Tick() {}
void VulkanBackend::SetPaused(bool paused) { paused_ = paused; }
void VulkanBackend::SetSpeed(double) {}
bool vulkan_available() { return false; }

} // namespace desktopsticker::wallpaper

#endif // DSTK_HAVE_VULKAN
