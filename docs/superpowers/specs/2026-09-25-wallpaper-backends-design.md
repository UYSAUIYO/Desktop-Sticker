# Desktop Sticker — 壁纸后端扩展（视频/动图/Web/3D 着色器 + 音频）设计文档

日期：2026-09-25
状态：设计已确认，待用户审阅
前置：`2026-09-25-dynamic-wallpaper-design.md`（动态壁纸基座，已实施）

## 1. 背景与目标

现有壁纸只支持**单一形状**：解码器产帧 → `IVideoSource` → D2D → 交换链。本设计把它扩展成**多后端**，并把音频补成真实能力：

1. **① 视频增强**：MP4/MOV/AVI/MKV/WMV（MF）+ WebM/VP8/VP9/AV1（FFmpeg 兜底）；循环、**调速**、**可开关的音频输出**；
2. **② 动图与图片序列**：GIF / 动态 WebP / 图片文件夹；
3. **③ Web 壁纸**：HTML/CSS/JS 渲染（WebView2），支持透明；
4. **④ 3D / 着色器**：**Vulkan + GLSL**，全屏着色器、glTF/glb/obj 模型、内置 GPU 粒子预设。

### 现状（已核实，作为设计前提）

- `DesktopSticker.WallPaper.dll` 已有：MF 为主 + FFmpeg 共享库兜底解码（`IVideoSource` / `open_video_source`）、D3D11+DXGI+DirectComposition 上屏（D2D 绘制并做 cover 缩放）、媒体库与性能副本、核心三项暂停策略、自有渲染线程（`FrameSchedulerLoop`，已 `CoInitializeEx` 且有消息循环）。
- FFmpeg 路径已加载 `swresample`，为音频解码留好了口子。
- `ResMon` 已验证 WebView2 可宿主在纯 Win32 窗口、异步完成必须**泵消息**等待。
- 本机 `C:\Windows\System32\vulkan-1.dll` 存在；GPU 为 NVIDIA RTX 4060 Laptop + Intel UHD。
- 项目当前**零 Vulkan**。

## 2. 已确认决策

| 项 | 决定 |
|---|---|
| 交付范围 | 四类一次全做，一个 spec |
| ④ 底层 | **真上 Vulkan + GLSL**，用 **Vulkan-Hpp**（许可 `Apache-2.0 OR MIT`） |
| ④ 内容边界 | 三类都做：全屏 fragment 着色器 / 模型（glTF·glb·obj + 内置相机与光照）/ 内置 GPU 粒子预设；参数走 uniforms + JSON，**不做可视化编辑器** |
| GLSL→SPIR-V | **子进程编译器 + `.spv` 缓存** |
| Vulkan 依赖策略 | 脚本按固定 commit 拉头文件 → gitignore；`__has_include` 门控；运行时动态加载系统 `vulkan-1.dll` |
| 后端归属 | 四个后端**都在现有 WallPaper DLL 内**（不新开 DLL） |
| 音频 | **全局开关 + 音量，默认静音**；**变速时同步变调**（重采样，不做 time-stretch） |
| 音频输出 | **WASAPI**（系统 API，无新增三方负载） |

## 3. 总体架构

### 3.1 后端抽象

```cpp
enum class BackendKind { Video, AnimatedImage, ImageSequence, Web, Shader3D };

struct WallpaperItem {
    std::wstring id, name;
    BackendKind kind = BackendKind::Video;   // 媒体库新增字段
    // 其余字段沿用基座
};

struct BackendContext {          // 渲染线程已就绪的资源
    HWND wallpaperWindow;        // WorkerW 子窗口
    int width, height;
    ID3D11Device* d3dDevice;     // 仅产帧型使用
    std::wstring exeDir;         // 负载 / 内置着色器所在目录
    std::wstring libraryRoot;    // 壁纸库根
    AudioEngine* audio;          // 可为 nullptr（无音频设备或用户关闭）
};

class IWallpaperBackend {
public:
    virtual ~IWallpaperBackend() = default;
    virtual bool Open(const WallpaperItem&, const BackendContext&) = 0;  // 渲染线程
    virtual void Close() = 0;
    virtual bool SelfPresenting() const = 0;      // true → 自己呈现，不走 D3dContext
    virtual bool ProduceFrame(std::vector<uint8_t>& bgra, int& w, int& h); // 产帧型
    virtual void Tick();                          // 自呈现型每轮调用
    virtual void SetPaused(bool) = 0;             // 自呈现型必须真正停
    virtual bool ApplyParams(const nlohmann::json&) = 0;
    virtual double TargetFps() const = 0;
    virtual const char* Name() const = 0;
    virtual BackendKind Kind() const = 0;
};
```

| 后端 | SelfPresenting | 呈现方式 | 音频 |
|---|---|---|---|
| Video | false | D3dContext（D2D→交换链） | 有（可开关） |
| AnimatedImage | false | 同上 | 无 |
| ImageSequence | false | 同上 | 无 |
| Web | **true** | WebView2 控制器填满窗口 | 有（可开关） |
| Shader3D | **true** | 自有 Vulkan 交换链 | 无 |

### 3.2 让位规则（最容易写错的一处）

`D3dContext` **只服务产帧型后端**。切到自呈现型时必须让它**让位**，否则 DComp visual 会盖住 WebView / Vulkan 交换链：

- 进入自呈现型：`D3dContext::Suspend()` —— `dcompVisual->SetContent(nullptr)` + `Commit()`（**不释放设备**，切回来还要用）
- 回到产帧型：`D3dContext::Resume()` —— 重新 `SetContent(swapChain)` + `Commit()`

这条会写成显式状态机（`PresentationArbiter`）并单测，避免"黑屏"或"被盖住"这类只能靠肉眼发现的问题。

### 3.3 线程模型

- 沿用**现有渲染线程**：所有后端在该线程 Open/Close/Tick/ProduceFrame。WebView2 的异步完成靠该线程的消息循环泵（ResMon 已验证该模式）。
- 音频引擎**自持线程**（见第 4 节）。
- **按需初始化**：切到视频壁纸时完全不触碰 Vulkan 与 WebView2。

## 4. 音频子系统

### 4.1 契约

```cpp
class IAudioSource {                       // 由 Video 后端实现（MF / FFmpeg 各一份）
    virtual bool HasAudio() const = 0;
    virtual bool ReadPcm(std::vector<uint8_t>& out, int& channels, int& sampleRate) = 0;
    virtual bool SeekToStart() = 0;
};

class AudioEngine {                        // 模块持有，后端无关
    bool Start();                          // 打开默认渲染设备；失败则整体降级为静音
    void Stop();                           // 停止并**释放设备**
    void SetSource(std::unique_ptr<IAudioSource>);   // 空 = 静音
    void SetMuted(bool);                   // 全局开关
    void SetVolume(float);                 // 0..1
    void SetSpeed(double);                 // 与视频同步的变速
    int64_t ClockUs() const;               // 有音频时的主时钟；无效返回 -1
};
```

### 4.2 时钟主从

- **有音频且未静音** → **音频时钟为主**：视频按音频时钟对齐，落后即丢帧（复用现有 `next_deadline_not_before` 的跳积压语义）。
- **无音频 / 已静音 / 无设备** → 沿用现有 QPC 时钟。
- 选择与漂移补偿写成**纯函数**（`clock_policy.cpp` 头），可单测。

### 4.3 变速

- 变速通过**重采样比**实现，**声调同步变化**（0.5× 低沉、2× 尖锐）；明确**不做** time-stretch。
- 音频侧用 `swresample` 改采样率；视频侧用 `frame_advance_policy` 决定丢帧/保持帧。

### 4.4 与暂停/生命周期的关系

- 暂停（手动 / 全屏遮挡 / 锁屏息屏）→ 音频一并暂停，并**释放设备**（不占用独占资源）。
- 恢复 → 重新打开设备；设备被别的应用独占或不存在 → 降级为静音并记日志，画面照常。
- 默认音频设备变更 → 重新打开设备（失败仍降级为静音）。

### 4.5 性能副本必须保留音轨（连带改动，易漏）

`build_transcode_args` 目前带 `-an`。改为保留音频：

```
-c:a aac -b:a 192k        # FFmpeg 内置 aac 编码器，LGPL 兼容
```

**禁止**引入 FDK-AAC 等有专利/许可问题的编码器。若源音频编码与 mp4 兼容，可优先 `-c:a copy`。

## 5. ① 视频后端

| 能力 | 实现 |
|---|---|
| 格式 | MP4/MOV/AVI/MKV/WMV → MF；WebM（VP8/VP9/AV1）等 MF 不支持的 → FFmpeg 兜底（基座已具备） |
| 循环 | 沿用基座（MF 回卷 / `av_seek_frame`） |
| 调速 | **新增**：`frame_advance_policy(speed, elapsedMs)` 决定本拍消费/保持几帧 |
| 音频 | **新增**：MF 路径开第二个 `IMFSourceReader` 取音频流；FFmpeg 路径用 `avcodec`+`swresample` |

变帧延迟：`IVideoSource` 增加可选 `FrameDurationMs()`（GIF/WebP 的逐帧延迟），为 0 时回落到 `Fps()`。

## 6. ② 动图与图片序列后端

- **GIF / 动态 WebP**：FFmpeg 直接解，接入现有 `IVideoSource`，几乎零架构改动。按逐帧延迟播放，受调速影响。
- **图片序列**（目录为源）：
  - 导入时把用户选的文件夹**逐字节复制**到 `media/<id>/frames/`（沿用"不改写源文件"的既有不变量）
  - 帧序按**自然排序**（`img2` 在 `img10` 前），只接受图片扩展名
  - 用已链接的 **WIC** 逐帧解码成 BGRA
  - 每帧延迟来自设置（默认 100 ms），受调速影响
  - 性能副本仍可行：`ffmpeg.exe` 的 image2 解复用器可把 `frames/` 转成 mp4

## 7. ③ Web 后端

> **实施偏差（2026-09-25 实测，已按此实现）**：原设计写的是"控制器宿主在壁纸窗口上"。
> 实测窗口化宿主在桌面嵌入窗口里**不合成** —— 环境/控制器/导航/窗口标题/窗口树/可见性/
> 尺寸全部正常，屏幕上却一个像素都没有（纯红测试页验证）；同一个窗口挪成顶层就正常。
> 因此改为 **WebView2 视觉宿主**（`CreateCoreWebView2CompositionController` +
> `put_RootVisualTarget`，并把该视觉经 `D3dContext::SetRootVisual` 挂成 DComp 根），
> 页面画面由我们自己的 DComp 合成 —— 本项目自己的 DComp 在同一种窗口上一直是好的。
> 其余（虚拟主机、沙箱、关闭浏览器特征、透明背景、`IsMuted`、`TrySuspend`）与设计一致。

- WebView2 控制器**不使用窗口化宿主**，走视觉宿主：画面合成进我们自己的 DComp 视觉树；
  虚拟主机映射到 `media/<id>/web/`；沿用"泵消息等异步完成"的既有写法。
- **安全**：用户 HTML/JS 跑在 WebView2 自带沙箱进程（无 Node、仅能访问映射目录）；**不注入任何宿主对象**；禁 DevTools 与右键菜单。
- **交互**：壁纸窗口本已 `WS_EX_NOACTIVATE` + `HTTRANSPARENT`，不抢焦点、不接点击。
- **透明**：`DefaultBackgroundColor` 置透明。因窗口是静态壁纸**之上**的 WorkerW 子窗口，透明处会露出原始壁纸（Wallpaper Engine 同款效果）。
- **音频**：页面可出声；`ICoreWebView2_8::put_IsMuted` 挂全局音频开关。
- **暂停/低功耗**：暂停 → `TrySuspend`（真正释放 CPU/GPU），恢复 → `Resume`。**如实说明**：网页帧率由页面自身 `requestAnimationFrame` 决定，我们唯一的杠杆是挂起/恢复，不要指望像视频那样精确控帧。
- 无 WebView2 运行时 → 该后端不可用，其它后端不受影响。

## 8. ④ 3D / 着色器后端（Vulkan + Vulkan-Hpp）

### 8.1 呈现

- 用 **Vulkan-Hpp**（`vk::raii::*`）；`VULKAN_HPP_DEFAULT_DISPATCHER` 从系统 `vulkan-1.dll` **动态解析全部入口**，不加导入库、不硬依赖。
- 在壁纸 HWND 上建 `vk::raii::SurfaceKHR`，**自持交换链直接呈现**。
- **明确不做** Vulkan 与 DComp 的 external memory 互操作（已知大坑且无必要）；与 D3dContext 的关系由 §3.2 的让位规则处理。
- Vulkan 不可用（无 loader / 无合适设备 / 交换链创建失败）→ 该后端不可用并降级，不影响其它后端。

### 8.2 三类内容

| 类型 | 范围 | 明确不做 |
|---|---|---|
| 全屏着色器 | 内置全屏三角形顶点着色器 + 用户 `.frag`；uniforms 含 `iTime` / `iResolution` + JSON 参数 | 多 pass、渲染到纹理链 |
| 模型 | 加载 `.gltf/.glb/.obj` 的位置/法线/UV/索引；内置**自动环绕相机** + 单向光 + Blinn-Phong | 骨骼动画/蒙皮、PBR 材质、阴影、多模型场景 |
| 粒子 | compute shader 写 storage buffer，实例化 billboard 绘制；参数（数量、寿命、重力、发射形状、配色）来自 JSON 预设 | 与模型/物体碰撞、自定义发射器脚本 |

- 内容目录约定：`media/<id>/shader/`（着色器、模型、粒子配置与可选的 `*.json` 参数文件）。
- 参数**只走 JSON**，不做可视化编辑器。

## 9. GLSL → SPIR-V 与 Vulkan 工具链

### 9.1 编译

- 子进程调用 `glslc.exe` / `glslangValidator.exe`（沿用现有 ffmpeg.exe 的子进程模式：显式参数数组、绝对路径、超时、无 shell）。
- 产物缓存到 `media/<id>/cached/<源文件哈希>.spv`，**只编译一次**；源文件变更即缓存失效。
- **内置着色器随包携带已编译 `.spv`**，因此**没有编译器时内置效果仍可用**。
- 用户自备 GLSL 且编译器缺失 → 提示"需要着色器编译器"，并给出获取方式（不改用未编译的源码）。

### 9.2 负载与工具链获取

沿用 FFmpeg SDK 那套做法（`tools/prepare_ffmpeg.ps1` 同构），**两个脚本各司其职**：

- `tools/prepare_vulkan.ps1`：按**固定 commit** 下载 Vulkan-Hpp 与 Vulkan-Headers 的归档，**校验 SHA-256** 后解压到 gitignored 目录（`tools/vulkan-sdk/`）；
- `tools/prepare_shaderc.ps1`：拉取固定版本的 shaderc / glslang 预编译工具（`glslc.exe` / `glslangValidator.exe`）到 `tools/shaderc/`（gitignored）；
- 项目用 `__has_include(<vulkan/vulkan.hpp>)` 门控 ④：**缺这些负载时编译照过，④ 降级为不可用**；
- 许可：`third_party/` 增加 Vulkan-Hpp（`Apache-2.0 OR MIT`，逐字保留两份文本）与 shaderc（`Apache-2.0`，内含 glslang `BSD-3-Clause`）的声明，并在 `THIRD_PARTY_NOTICES.md` 增节。

### 9.3 实施分阶段（本 spec 覆盖四类，但计划与交付分批）

四类一次成 spec，实施仍**分阶段推进、每阶段独立可验收**，顺序按风险与依赖排：

1. **阶段 0（先做，最高风险）**：Vulkan 最小可运行验证 —— 一个 SPIR-V 着色器铺满桌面。**此步不通过则 ④ 的方案要回头改。**
2. **阶段 1**：后端抽象 + `PresentationArbiter` + `BackendKind` 识别与库迁移（四项的共同地基）。
3. **阶段 2**：① 视频增强（调速 + 音频子系统）。
4. **阶段 3**：② 动图与图片序列。
5. **阶段 4**：③ Web 后端。
6. **阶段 5**：④ 着色器 → 模型 → 粒子（依次叠加）。
7. **阶段 6**：设置页汇总、文档、端到端验收。

## 10. 媒体库与存储

```
media/<id>/
├─ source.<ext>        # ① 视频 / ② 动图
├─ frames/…            # ② 图片序列（逐字节复制的帧，自然排序）
├─ web/…               # ③ Web（index.html + 资源）
├─ shader/…            # ④ 着色器 / 模型 / 粒子配置 / *.json
├─ cached/…            # ④ 编译好的 .spv 缓存（可删，可重建）
├─ poster.png
└─ variants/…          # 仅 ① 视频
```

- `library.json` 增加 `kind` 字段并**升版本号 + 自动迁移**（旧条目按"有 `source.*` 即 Video"推断，沿用既有迁移约定）。
- 存储分类（ResMon 的 9 类）不变；`cached/` 与 `shader/` 归入既有 `wallpaper` 分类。
- 删除条目时**只删库内副本**，绝不触碰用户原始文件夹。

## 11. 设置页与 UI

- **导入入口按类型分流**：视频文件 / 图片文件 / 图片文件夹 / 网页文件夹 / 着色器文件夹；也可由文件选择器的结果自动识别 `BackendKind`。
- **播放速度**：0.25×–4× 档位（滑杆或下拉）。**作用范围**：①② 直接作用于播放节奏；④ 缩放 `iTime` 推进速率（着色器/粒子/相机随之变快变慢）；**③ Web 不适用** —— 网页的动画节奏由页面自身的 `requestAnimationFrame` 决定，我们无法介入，故该控件在 Web 类型下**禁用并说明**。
- **音频**：全局开关（默认关）+ 音量滑块；对无音轨的类型（动图/序列/Shader3D）**置灰并说明原因**；Web 下可用（页面出声），但需说明"页面是否真的发声由网页自己决定"。
- **档位（原画/均衡/省电）只对视频有意义** → 非视频类型禁用并说明。
- **打开配置目录**按钮（便于用户放 `.frag` / `.gltf` / JSON 参数）。
- 壁纸卡片显示当前后端的类型标签（视频 / 动图 / 序列 / 网页 / 3D）。

## 12. 纯函数与测试

沿用 dtest 与既有做法（逻辑提成 header-only 纯函数）。**现有 129 项必须保持全绿**。

| 模块 | 覆盖点 |
|---|---|
| `frame_advance_policy` | 调速下的丢帧/保持帧：1×、0.25×、4×、超长间隔、速度为 0 的非法值 |
| `BackendKind` 识别 | 扩展名 → Video/AnimatedImage；目录内容 → ImageSequence/Web/Shader3D；边界：空目录、只含图片、含 `index.html`、含 `.frag`、含 `.gltf`、同时含多种 |
| 自然排序 | `img2` < `img10`；大小写与前后导零；非数字后缀 |
| `clock_policy` | 有/无音频时的主时钟选择与漂移补偿；音频无效时回落 QPC |
| SPIR-V 缓存键 | 源文件哈希 → 缓存路径；源变更失效 |
| JSON 参数合并 | 缺字段取默认、类型不符忽略、数值越界钳制 |
| `PresentationArbiter` | 产帧型 ↔ 自呈现型的让位状态机（含连续切换、Open 失败回滚） |

## 13. 错误处理与降级

| 情形 | 行为 |
|---|---|
| Vulkan 不可用（无 loader / 无设备 / 交换链失败） | ④ 不可用，其余后端不受影响；托盘/设置页说明 |
| WebView2 运行时缺失 | ③ 不可用，其余不受影响 |
| 着色器编译失败 / SPIR-V 无效 | 回退到上一个可用壁纸或静态，不黑屏；日志给出编译器输出 |
| 无音频设备 / 设备被独占 | 降级为静音并记日志，画面照常 |
| 音频设备中途变更 | 重新打开；失败仍降级为静音 |
| 图片序列含非图片文件 | 跳过并计入 warnings，不崩溃 |
| `library.json` 缺 `kind`（旧数据） | 自动迁移为 Video，不崩溃 |
| 驱动崩溃（Vulkan） | 本轮**接受**无法挽救；但需保证**下次启动不再自动加载同一个着色器**（记录失败标记） |

## 14. 人工验收

- ① MP4 / WebM / MOV 三类各能循环播放；**调速** 0.5× 与 2× 画面节奏正确；**打开音频开关后能出声、关掉即静音**；音量滑块生效
- ① 性能副本（均衡/省电）**保留音轨**，切到副本后仍有声音
- ② GIF 与动态 WebP 正常播放且逐帧延迟正确；图片文件夹导入后按自然序播放，源文件夹未被改动
- ③ 网页壁纸正常渲染；带透明背景的页面能露出原始桌面壁纸；暂停时 CPU 占用明显下降（`TrySuspend` 生效）
- ④ 一个 GLSL 全屏着色器铺满桌面并随时间变化；`.gltf` 模型正常加载并环绕；内置粒子预设正常；用户改 `.frag` 后重编译生效且命中缓存
- ④ Vulkan 不可用时（临时改名/移除负载）**编译仍通过**，④ 降级、其余后端正常
- 产帧型 ↔ 自呈现型来回切换**不黑屏、不互相遮盖**
- 分区拖拽/滚轮/双击、搜索启动器、桌面时钟、ResMon **全部无回归**
- 退出后无游离窗口、无残留进程、桌面图标正常还原

## 15. 未纳入范围

- 变速**不变调**（time-stretch / 相位声码器）
- Vulkan 与 DComp 的 external memory 互操作
- 骨骼动画/蒙皮、PBR 材质、阴影、多模型场景
- 音频驱动着色器（FFT → uniforms）
- 壁纸可视化编辑器
- 多显示器各自播放（沿用基座：主显示器）
- 桌面图标层之上的交互式壁纸（壁纸不可交互）

## 16. 风险与对策

| 风险 | 对策 |
|---|---|
| **Vulkan 在 WorkerW 子窗口上自持交换链**（最高，完全未验证） | **计划第一步做最小可运行验证**：一个 SPIR-V 着色器铺满桌面；跑通再叠模型与粒子。失败则④按 §13 降级，不影响其余三类 |
| 自呈现型与 D3dContext 的让位写错 → 黑屏 / 被盖住 | `PresentationArbiter` 显式状态机 + 单测；人工验收含来回切换 |
| WebView2 跑在专属渲染线程（基座只在主 UI 线程验过） | 沿用"泵消息等异步"写法；Open 失败可降级；先做 Web 后端的独立验证 |
| A/V 不同步 | 音频时钟为主 + 漂移补偿（纯函数可测） |
| 驱动崩溃带崩宿主 | 本轮接受（与"进程内 DLL"的既有取舍同源）；记录失败标记避免下次自动加载同一着色器 |
| 变调音频不好听 | 已确认按"同步变调"实现；如需不变调另开任务 |
| 三个新负载（Vulkan 头、shaderc/glslang）体积与许可 | 全部脚本拉取 + 哈希校验 + gitignore + `third_party/` 声明；缺负载只降级 |
| 首次构建未跑脚本导致④静默不可用 | 构建后由设置页与日志明确提示"④ 不可用：缺少 Vulkan 负载" |
