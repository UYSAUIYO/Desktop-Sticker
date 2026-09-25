# Desktop Sticker — 动态桌面壁纸（DesktopSticker.WallPaper）设计文档

日期：2026-09-25
状态：设计已确认，待用户审阅
参考实现：`https://github.com/1114656/MotionWallpaper`（MIT）

## 1. 背景与目标

为 Desktop Sticker 增加**动态桌面壁纸**：把视频作为桌面壁纸循环播放，位于原生桌面图标与既有分区卡片**之下**，不干扰分区收纳、搜索启动器与桌面时钟。

能力目标：

1. 导入视频进本地壁纸库，逐字节复制源文件，**永不改写源文件**；
2. 播放原画：系统解码器（Media Foundation）为主，系统无法解码该素材时自动改用随包 FFmpeg 实时解码；
3. 可选性能副本：用未经修改的 `ffmpeg.exe` 后台转码出 `balanced` / `power-saver` 副本，供配置较低的机器或高码率素材使用；
4. 在打游戏 / 锁屏 / 息屏时自动暂停，避免持续烧 GPU；
5. 库与设置可管理，存储位置自动落在剩余空间最大的固定盘。

非目标（明确排除，见第 12 节）：媒体分组、回收站事务、库迁移事务、闲置屏保、冻结帧入 DWM、多显示器各自播放。

## 2. 已确认决策

| 项 | 决定 |
|---|---|
| 交付范围 | 完整功能一次做完 |
| 播放主路径 | MF 为主 → MF 报告无法解码该素材时才用 FFmpeg 共享库兜底 |
| 转码副本 | `ffmpeg.exe` 子进程生成 `balanced` / `power-saver` |
| 上屏 | D3D11 + DXGI 交换链 + DirectComposition |
| 组件形态 | 新工程 `DesktopSticker.WallPaper.dll`，宿主进程内 `LoadLibrary` |
| 媒体库范围 | 中等：库 + 缩略图 + 切换（不做分组 / 回收站事务 / 库迁移事务） |
| 暂停策略 | 核心三项：全屏遮挡暂停、锁屏/息屏停止、手动暂停 + 回桌面恢复 |
| 负载获取 | 固定版本 + SHA-256 校验下载；二进制不入 git |
| 管理界面 | 并入现有 WinUI 设置页 |
| 存储位置 | 首次启用时自动选剩余空间最大的固定盘，选一次后固定 |

## 3. 总体架构与边界

### 3.1 组件

```
Desktop Sticker.exe（WinUI 3 宿主）
├── Host：加载 DesktopSticker.Features.dll（既有，分区 / 搜索 / 时钟）
└── Host：加载 DesktopSticker.WallPaper.dll（新增，本设计）
       ├── WallPaper / 桌面嵌入与窗口生命周期
       ├── Decode（MediaFoundationDecoder / FfmpegDecoder / FfmpegApi）
       ├── Present（D3dDevice / SwapChain / DCompTarget / FrameScheduler）
       ├── Media（MediaLibrary / ThumbnailGenerator / FfmpegTranscoder）
       └── Policy（PausePolicy / VariantPolicy / DesktopHostPolicy / StoragePlacement）
```

`DesktopSticker.WallPaper` 为纯 C++20 Win32 DLL（自带 pch，允许 D3D/MF，**不引入 WinRT**，与 Features 同规矩），输出到 EXE 旁。

### 3.2 EXE ↔ WallPaper 边界

新建独立接口，**不复用 `IFeatureModule`**，照既有边界模式同构：

```cpp
// include/desktopsticker/IWallPaperModule.h
namespace desktopsticker {

struct WallPaperEvents {
    std::function<void()> libraryChanged;         // 库增删改，设置页刷新
    std::function<void()> playbackStateChanged;   // 播放状态变化，设置页刷新
};

enum class VariantKind { Original, Balanced, PowerSaver };

struct WallPaperItem {
    std::wstring id, name, sourceFile;            // 相对库根的文件名
    bool hasPoster = false;
    bool hasBalanced = false, hasPowerSaver = false;
    uint64_t sourceBytes = 0;
};

struct WallPaperSettings {
    bool enabled = false;
    std::wstring activeId;                        // 当前壁纸
    VariantKind preferred = VariantKind::Original;
    bool pauseOnFullscreen = true, pauseOnLock = true;
    std::wstring libraryRoot;                     // 记录用，只读回显
};

class IWallPaperModule {
public:
    virtual ~IWallPaperModule() = default;
    virtual bool Init(const WallPaperEvents&) = 0;
    virtual bool Start() = 0;                     // 桌面嵌入初始化；失败须能降级
    virtual void Stop() = 0;
    virtual void Shutdown() = 0;

    virtual std::vector<WallPaperItem> ListItems() = 0;
    virtual bool Import(const std::wstring& srcPath, std::wstring& outId) = 0;
    virtual bool Rename(const std::wstring& id, const std::wstring& name) = 0;
    virtual bool Remove(const std::wstring& id) = 0;
    virtual HICON GetThumbnail(const std::wstring& id, int size) = 0;  // 模块持有缓存，调用方不得 DestroyIcon

    virtual WallPaperSettings GetSettings() = 0;
    virtual bool SetSettings(const WallPaperSettings&) = 0;
    virtual bool RegenerateVariant(const std::wstring& id, VariantKind) = 0;
    virtual bool ChangeLibraryRoot(const std::wstring& newRoot) = 0;   // 手动更换存储位置
};

} // namespace desktopsticker

extern "C" __declspec(dllexport) desktopsticker::IWallPaperModule* CreateWallPaperModule();
extern "C" __declspec(dllexport) void DestroyWallPaperModule(desktopsticker::IWallPaperModule*);
```

`GetThumbnail` 沿用 Features 的既有约定：返回模块缓存持有的 HICON，调用方不得 `DestroyIcon`。

### 3.3 宿主加载与降级

`Host` 增加一条并行加载路径（`LoadWallPaper()` / `UnloadWallPaper()`），**不做泛型抽象**——两个 DLL 不值得引入模板。约定：

- EXE 只消费 `IWallPaperModule.h`，不消费该 DLL 的任何其他头文件；接口即 ABI，两端必须同步重编。
- DLL 缺失、`LoadLibrary` 失败、`Create` 返回空或 `Init`/`Start` 失败，**一律降级为"壁纸功能不可用"**，不得影响分区收纳、搜索启动器与桌面时钟；App 提示用户但不阻断启动。
- 异常不得穿透到 `OnLaunched`，与 `Host::LoadFeatures` 既有的 try/catch + 资源回收写法一致。

## 4. 解码与呈现管线

### 4.1 三条来源，一条呈现路径

```
原画 source.<ext>
  ├─ [主] MF：IMFSourceReader + MFCreateDXGIDeviceManager
  │        → 硬件解码输出 NV12 DXGI 纹理
  │        → MF 报告该素材无法解码 → 落到下级
  ├─ [兜底] FFmpeg：LoadLibrary 动态解析 avformat/avcodec/avutil/swscale
  │        → 软解帧 → 上传 D3D11 纹理
  └─ [可选] variants\balanced-<rev>.mp4 | power-saver-<rev>.mp4
        ↓
  统一呈现：D3D11 着色器 NV12→RGB → DXGI 交换链 → Present
        ↓
  DirectComposition visual 挂在壁纸窗口上
```

**为什么不用 `IMFMediaEngine`**：它自带呈现管线，无法注入 FFmpeg 兜底的帧。既然要求双解码器可切换，必须自持呈现层。代价是循环、定位、帧调度需要自己实现。

**帧调度**：按 QPC 单调时钟维护下一帧期限；处理完成后等待剩余时间，帧未就绪时短重试，错过期限则跳过积压。非播放状态停止调度。

### 4.2 档位选择策略

`VariantPolicy`（纯函数，可单测）：

- `Original`（默认）：播放原画；MF 不可解码 → FFmpeg 兜底；两者都不行则保持最后一帧并报状态。
- `Balanced` / `PowerSaver`：若对应副本存在则播放副本，否则回落到原画路径并提示副本未生成。

不在运行时做"卡顿测量"来自动降档；档位由用户在设置页选择，自动推荐留给后续迭代。

## 5. 桌面嵌入与窗口层级

壁纸窗口与原生桌面图标、分区卡片、时钟的层级关系：

```
Progman / WorkerW（含 SHELLDLL_DefView 的那层）
  └── SysListView32（原生桌面图标）
WorkerW（DefView 之后那个，壁纸宿主）
  ├── ZoneWindow / ClockWindow（既有，HWND_TOP 侧）
  └── WallPaperWindow（新增，HWND_BOTTOM）
```

- 壁纸窗口 `SetParent` 到**壁纸宿主 WorkerW**，`SetWindowPos` 置 `HWND_BOTTOM`，因此天然位于图标与分区卡片之下。
- 分区卡片用 `HWND_TOP`/`HWND_BOTTOM` 控制层级，分区变更后需**重申壁纸窗口的底部位置**，避免被顶到卡片之上。
- 窗口样式：`WS_EX_NOACTIVATE`，不抢焦点、不接管点击；不处理 `WM_MOUSEACTIVATE`。
- **必须排除出"双击桌面空白"检测**（桌面时钟曾踩过该坑），否则双击壁纸会误触干净桌面。
- **干净桌面模式**：只隐藏磁贴与原生图标，壁纸继续播放。
- **自包含实现**：本组件自行发现壁纸宿主 WorkerW，**不修改 Features 中那段 load-bearing 的 `DesktopShellIntegration`**（该处历史上反复回归）。此处接受少量逻辑重复以换取既有嵌入路径零风险。
- **降级**：嵌入失败退化为置底普通窗口，播放功能不中断（与分区/时钟既有降级约定一致）。

## 6. 媒体库与存储

### 6.1 配置与库分离

配置留在 `%APPDATA%\DesktopSticker\`（否则启动时无从得知库的位置），大文件库落在自动选出的盘：

```
%APPDATA%\DesktopSticker\wallpaper.json          # 开关、当前 id、档位、暂停规则、库根记录
<选定盘>:\DesktopSticker\Wallpaper\
├─ library.json                                  # 条目：id / 名称 / 源文件名 / 封面 / 副本 / 导入时间
└─ media\<id>\
   ├─ source.<原扩展名>                           # 逐字节复制，永不改写
   ├─ poster.png                                 # 缩略图 / 封面
   └─ variants\balanced-<rev>.mp4                # ffmpeg.exe 后台转码，可删可重建
                power-saver-<rev>.mp4
```

壁纸设置自持于 `wallpaper.json`，**不并入 `AppConfig`、不动 `layout.json`**，避免 ABI 与布局版本牵连。设置页通过 `IWallPaperModule::GetSettings/SetSettings` 读写。

### 6.2 存储位置自动选择

首次启用时执行一次，结果持久化：

1. `GetLogicalDriveStringsW` 枚举，`GetDriveTypeW == DRIVE_FIXED` 过滤 —— 只考虑固定盘，排除可移动盘、网络盘与光驱（否则插着的大容量移动盘会被选中，拔盘即失效）。
2. `GetDiskFreeSpaceExW` 取 `ullFreeAvailable`（调用者可用空间，尊重配额），取最大值；并列时取盘符最小者，保证结果确定。
3. 在选定盘创建 `DesktopSticker\Wallpaper\`，把**盘符 + 卷序列号 + 根目录文件 ID** 记入 `wallpaper.json`。

**选一次后固定，不自动迁移**：库内含上百 MB 源文件副本，跟随剩余空间漂移会导致反复搬库。与"中等范围不做迁移事务"的决策一致。更换位置由设置页"更改存储位置"手动触发：复制 + 逐文件校验 + 切换记录，**旧位置保留不删**。

### 6.3 启动校验与降级

每次启动验证：库根存在、位于固定盘、且卷序列号与根目录文件 ID 与记录一致（防止盘符被复用后写入别的卷）。任一不符 → 壁纸功能降级为不可用、设置页明确提示并提供重选，**不影响其余功能，且绝不向未验证的路径写入或清理**。

### 6.4 媒体操作（中等范围）

- **导入**：逐字节复制源文件到 `media\<id>\source.<ext>`；后台排队生成 `balanced` 副本与 `poster.png`。队列串行，一次一个转码进程。
- **列表 / 切换 / 重命名 / 删除**：删除仅删除库内副本，**不触碰用户原始文件**。
- **不提供**：分组、回收站事务、库迁移事务、重复内容去重、排序规则。
- **容错**：`library.json` 损坏或条目指向的文件缺失 → 跳过/重建该条，不崩溃（对齐既有 config 容错约定）；写入一律先写临时文件再原子替换。

## 7. 播放策略与线程模型

### 7.1 暂停优先级

`PausePolicy` 纯函数 reducer，按固定优先级归约到目标状态：

1. 会话锁定 / 显示器关闭 → 停止解码与呈现
2. 用户手动暂停 → 暂停
3. 全屏窗口遮挡 → 暂停。判定枚举**所有可见、未最小化、且未被 DWM 隐藏的顶层窗口**，不只看焦点窗口
4. 切回桌面 → 恢复播放

暂停时停止解码与帧调度（不保留"快速恢复窗口"这种额外状态）。

### 7.2 线程模型

- WallPaper 模块自持**一条专用线程**，在该线程上创建窗口、跑消息循环与帧调度循环。
- 与宿主通过 `PostMessage` / 命名事件交互；**不触碰宿主 UI 线程的窗口与布局状态**，与既有"UI 线程持有全部窗口与布局数据"的约定不冲突。
- D3D11 设备与 DComp 目标全部在该线程内创建与销毁，避免跨线程使用。
- 退出（托盘"退出"或 `Shutdown`）：`Stop` 解码 → 销毁交换链与 DComp 目标 → 销毁窗口 → 退出线程；**不留游离窗口**，桌面图标与分区还原不受影响。

## 8. 第三方组件、许可与声明

### 8.1 使用方式与合规要点

| 组件 | 使用方式 | 许可 | 合规要求 |
|---|---|---|---|
| FFmpeg | `ffmpeg.exe` 子进程（转码）+ `LoadLibrary` 动态加载 `avformat/avcodec/avutil/swscale`（解码兜底） | LGPL v3 | **只动态加载，绝不静态链接**；不向 Windows 注册系统解码器；保留许可证与声明 |
| OpenH264 | 所固定 FFmpeg 构建内集成的编码器 | BSD | 保留版权、条件与免责声明；声明不主张 Cisco 对官方预编译二进制的专利许可适用于该构建 |
| MotionWallpaper | 参考其实现方式，并移植部分策略层代码 | MIT | 保留其 MIT 版权声明 |

**H.264 专利**：正式分发前，发布者仍需独立确认适用地区的 H.264 专利许可要求。此声明原样保留，不弱化。

### 8.2 固定负载版本

沿用参考项目锁定的构建，保证可复现：

```
BtbN/FFmpeg-Builds  Windows x64 LGPL shared build 8.1
ffmpeg-n8.1.2-53-g1005b294ff-win64-lgpl-shared-8.1.zip
SHA-256 a654407793b1caef118550de3b99e46299dcabc6649ccf9a3a325f41ff4ea414
```

获取脚本 `tools/prepare_ffmpeg.ps1`：按上述版本下载、**校验 SHA-256 后**解压到 `tools/ffmpeg/`（`Tools/ffmpeg` 在 Windows 上即现有 `tools/ffmpeg`，与既有 PowerShell 脚本同目录，不另造层级）。`tools/ffmpeg/` 进 `.gitignore`，二进制不入库。

### 8.3 运行期负载位置与加载方式

仓库 `tools/ffmpeg/` 与 EXE 输出目录不是同一棵树，因此分两级：

- **构建期**：`PostBuildEvent` 把 `tools/ffmpeg/` 下的 `ffmpeg.exe`、`av*.dll` 与两份许可文本复制到 `<exeDir>\ffmpeg\`；
- **运行期**：以 `<exeDir>\ffmpeg\` 为唯一查找位置。`ffmpeg.exe` 以绝对路径 CreateProcess；共享库用 `LoadLibraryExW` 配 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`，**限定搜索范围**，不从当前工作目录或 `PATH` 解析，避免 DLL 劫持。

负载缺失时：`ffmpeg.exe` 路径不可用 → 只禁用副本生成；av*.dll 不可用 → 只禁用 FFmpeg 解码兜底。两者都不影响原画（MF）播放。

### 8.4 声明文件落点

`third_party/` 已存在（含 `nlohmann/json.hpp`），声明放此处：

| 文件 | 内容 |
|---|---|
| `THIRD_PARTY_NOTICES.md`（仓库根，新增） | 按本项目实际用法撰写，覆盖上述三条组件 |
| `third_party/FFmpeg-NOTICE.txt` | 动态链接、不静态链接、不注册系统解码器、锁定版本 + SHA-256 |
| `third_party/LICENSE-FFmpeg.txt` | LGPL v3 全文 |
| `third_party/OpenH264-LICENSE.txt` | BSD 全文 |
| `third_party/MotionWallpaper-MIT.txt` | MIT 版权声明 |

运行期随负载保留：由 `prepare_ffmpeg.ps1` 把仓库内的 `third_party/LICENSE-FFmpeg.txt` 与 `third_party/OpenH264-LICENSE.txt` 复制为 `tools/ffmpeg/LICENSE-FFmpeg.txt`、`tools/ffmpeg/LICENSE-OpenH264.txt`，再由 8.3 的构建事件随二进制一同带到 `<exeDir>\ffmpeg\`。不依赖 BtbN 压缩包内自带的许可文件名。

README 增加"第三方组件与许可"章节；AGENTS.md 补充新工程、负载脚本与声明文件说明。

## 9. 构建、打包与测试

### 9.1 构建

- 新增 `Desktop Sticker/DesktopSticker.WallPaper/DesktopSticker.WallPaper.vcxproj`，加入 `Desktop Sticker.sln`，配置 Release x64。
- 依赖库：`d3d11.lib dxgi.lib dcomp.lib mfplat.lib mf.lib mfuuid.lib ole32.lib`（外加既有约定所需项）。
- 新文件**必须手动加入 `.vcxproj` 与 `.filters`**（仓库无 globbing）。
- `PostBuildEvent`：把 `DesktopSticker.WallPaper.dll` 复制到 EXE 目录（与既有 Features DLL 复制方式一致）。
- `tools/ffmpeg/` 缺失时，`build.bat` 提示运行 `tools/prepare_ffmpeg.ps1`；不因缺失而中断编译（壁纸功能降级即可）。

### 9.2 单元测试

沿用 `dtest` 框架，**采纳参考项目"把策略提炼成纯函数头文件再测"的做法**，新增用例：

- `PausePolicy` 优先级归约（遮挡 / 锁屏 / 息屏 / 手动 → 目标状态）
- `VariantPolicy` 档位选择与副本缺失回落
- `StoragePlacement` 盘符选择（过滤非固定盘、取最大可用、并列取最小盘符）
- `FrameScheduler` 期限计算（QPC → 等待 / 跳帧）
- `ffmpeg.exe` 命令行构造（参数正确、输出路径、**不覆盖源文件**）
- `library.json` 往返与损坏容错
- 桌面宿主可用性判定

既有 **38 项必须保持全绿**。

### 9.3 人工验收

- 1080p / 4K 的 H.264 与 HEVC 长时间循环播放
- 全屏应用切进切出 → 暂停 / 恢复；锁屏解锁 → 停止 / 恢复；手动暂停开关
- 干净桌面模式切换时壁纸继续播放，双击壁纸**不**误触干净桌面
- 分区拖拽 / 滚轮 / 双击打开等既有交互无回归；时钟无回归
- 存储位置首次选择落在预期盘符；人为使库根失效后功能降级且其余功能正常
- 退出后无游离窗口、桌面图标正常还原
- 观测 CPU / GPU Video Decode / 工作集 / 句柄数
- 缺少 `tools/ffmpeg/` 时：原画（MF）仍可播放，仅副本生成不可用，且有明确提示

## 10. 风险与降级

| 风险 | 应对 |
|---|---|
| DComp + 交换链挂在 WorkerW 子窗口上是全新路径，本机行为待实测 | 先做最小可运行验证；嵌入失败降级为置底普通窗口 |
| MF `IMFSourceReader` + DXGI 设备管理器在混合显卡机器上探测与实解可能不一致 | 探测失败即回落 FFmpeg；日志记录实际路径 |
| 新增 D3D 依赖是否干扰既有 ULW 渲染 | 理论无关（不同窗口与设备），但人工验收必须覆盖分区/时钟无回归 |
| 盘符被复用导致写入错误卷 | 启动校验卷序列号 + 根目录文件 ID，不符即降级 |
| `ffmpeg.exe` 转码进程异常/挂起 | 串行队列 + 超时终止；单个转码失败不影响播放与其余条目 |
| 本机 Debug CRT 环境异常 | 全部验证走 Release，与既有约定一致 |

## 11. 验收标准

1. 导入视频后能作为壁纸循环播放，原生桌面图标与分区卡片均显示在壁纸之上。
2. 原画在 MF 可解码时走 MF；人为构造 MF 不可解码的素材时自动回落 FFmpeg 成功播放。
3. `balanced` / `power-saver` 副本可由 `ffmpeg.exe` 生成、删除、重建，且**源文件字节不变**。
4. 全屏遮挡、锁屏/息屏、手动暂停三项均按预期暂停/恢复。
5. 库与设置存于预期位置；配置与库损坏均不导致崩溃。
6. 缺少 FFmpeg 负载或 DLL 缺失/失败时，应用其余功能完全正常。
7. 既有 38 项单测全绿，新增用例全绿。
8. 许可与声明文件齐备，FFmpeg 为动态加载、无静态链接。
9. 退出后无游离窗口，桌面图标与分区正常还原。

## 12. 未纳入范围

以下为明确排除项，本期不做；如需请另开设计：

- 媒体分组、回收站事务、媒体库迁移事务与卷所有权校验
- 闲置屏保模式、冻结帧捕获入 DWM、低内存资源释放、30 秒快速恢复窗口
- 多显示器各自播放（本期单屏：主显示器）
- 运行时卡顿测量与自动降档
- 音频播放（壁纸静音，不做音频输出）
- 从网络下载壁纸内容
