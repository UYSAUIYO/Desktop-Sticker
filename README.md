# Desktop Sticker

Windows 11 桌面整理工具：把杂乱的桌面图标自动分类收纳进可自由拖动的**磁贴卡片**，内置 **双击空格搜索启动器** 与 **桌面时钟组件**（时间 / 日出日落 / 实时天气）。WinUI 3 前端 + Win32/Direct2D 功能层，未打包自包含，无需安装运行时。

## 功能一览

### 桌面分区收纳

- **自动分类**：首次启动把桌面图标按关键词规则分类到 应用 / 开发工具 / 办公软件 / 浏览器 / 影音娱乐 / 实用工具 / 社交聊天 / 游戏 / 文件夹 / 其他 十个分区；新放入桌面的文件由目录监视自动归档。
- **四列布局**：左右各两列贴边、卡片高度动态铺满到任务栏；每列卡片数（4 张或 5 张）可在设置中切换并立即重排。
- **卡片交互**：按住卡片任意空白处拖动、边缘缩放（位置尺寸自动保存）；双击标题折叠/展开；滚轮平滑滚动查看更多磁贴。
- **磁贴交互**：双击打开文件；拖动磁贴到其他分区即移动、拖出到空白处即恢复为原生桌面图标；右键菜单支持打开 / 移出。
- **分区管理**：右键标题重命名 / 删除分区；双击桌面空白切换**干净桌面**（隐藏磁贴与原生图标，再次双击恢复）。
- **一键还原**：托盘"恢复桌面"把所有被收纳的图标放回原始位置并重新显示。

### 搜索启动器

- **双击空格**唤起（也可在设置中改为 `Alt+Space` 等自定义组合键），`Esc` 关闭；
- 搜索范围：桌面文件、文档/下载/图片/视频/音乐目录、开始菜单应用、手动添加的应用；
- 支持**拼音首字母**检索（如 `wx` → 微信）、键盘方向键导航、回车打开；
- 结果按来源分组展示，图标异步加载并缓存。

### 桌面时钟组件

- 顶部居中悬浮卡片：**模拟表盘**（秒针平滑扫动）+ 大号数字时间 `HH:MM:SS` + 国家·省·市地区显示；
- **日出日落**：太阳-地球轨道动画，地球位置 = 今天已度过的时间（日出在左端、正午顶点、日落在右端，夜间走暗弧），时刻按本地真实日出日落数据计算；
- **当前小时天气**：气温、天气现象、风向风力，彩色天气图标（[QWeather S2](https://github.com/qwd/WeatherIcon) 图标集）；
- 数据来源：IP 定位（**直连取真实 IP，不走代理**）+ [Open-Meteo](https://open-meteo.com) 免费接口，无需任何 API 密钥；后台每 30 分钟刷新，失败快速重试。

### 动态桌面壁纸

- 四类壁纸来源，统一在**原生桌面图标与分区卡片之下**播放，不影响分区收纳与交互：
  - **视频**：MP4 / WebM / MOV 等常见格式，循环播放，支持 0.25×–4× 调速与声音开关（含音量）；
  - **动图 / 图片序列**：GIF、动态 WebP，或一个图片文件夹（按自然序播放，默认 100ms/帧）；
  - **网页**：HTML/CSS/JS 页面（WebView2 沙箱运行，透明处露出原始桌面壁纸）；
  - **3D / 着色器**：Vulkan 自持交换链直接呈现，内置两个场景 —— **全屏着色器**（等离子）与 **3D 实体**（32 面截角二十面体，按面上色 + 环绕相机 + 单向光 Blinn-Phong）。用 `shader/scene.json` 的 `{"scene":"model"|"fullscreen"}` 切换；两者都在 60fps。gltf/glb/obj 文件加载、粒子预设与用户 GLSL 的运行时编译尚未做。
- **解码双路径**：系统解码器（Media Foundation）为主；系统无法解码该素材时，自动改用随包 FFmpeg 共享库实时解码；动图刻意优先 FFmpeg（MF 只能出首帧）；
- **按显示尺寸解码**：4K 素材在 1080p 屏上只转屏幕大小的像素，不再把多出来的分辨率白转一遍；
- **性能副本**：用未经修改的 `ffmpeg.exe` 后台转码出「均衡」「省电」副本，供高码率素材或配置较低的机器使用；副本可随时重建或删除，**源文件始终保持字节不变**；
- **自动暂停**：全屏应用遮挡时暂停、锁屏/息屏时停止、支持手动暂停，切回桌面自动恢复；网页类型暂停时用 `TrySuspend` 真正释放 CPU/GPU；
- **媒体库**：导入文件或文件夹（类型自动识别）、缩略图、列表切换、重命名、删除；
- **存储位置**：首次启用时自动选剩余空间最大的**固定盘**（`<盘>:\DesktopSticker\Wallpaper\`），记录卷序列号与根目录文件 ID，防止盘符被复用后写错卷。

### 资源管理器

基于 **Microsoft WebView2** 的只读资源查看器（独立窗口，托盘菜单或 `--resmon` 打开），三页签：

- **CPU**：进程总占用 + **按线程**明细（线程名可读：UI/分区渲染、热键钩子、目录监视、壁纸渲染与帧调度、暂停监控、天气后台、转码工作），并单列子进程（转码时的 `ffmpeg.exe`）；每 2 秒刷新。
- **内存**：工作集 / 私有提交 / 峰值工作集 + **本程序自己的模块**占用（自写代码 + 随包 ffmpeg 解码库，按映像大小降序）。**不含系统公共 DLL 与第三方框架**（SHELL32、显卡驱动、Windows App SDK、DirectML 等一律排除）。
- **存储**：程序占用合计 + 分类占比条 + 明细 + "重新计算"。分类为壁纸媒体库 / FFmpeg 负载 / 天气图标资源 / 调试符号 / 程序主体 / 运行时与框架 / 配置与布局 / 日志 / 其他。

**只读**：不会创建、修改或删除任何文件；扫描在工作线程执行，不阻塞界面。

### 设置

Win11 系统设置风格界面（Mica 背景卡片、跟随系统深浅色），通过托盘菜单或 `--settings` 启动参数打开：

- 搜索范围（桌面 / 已知文件夹 / 开始菜单应用 / 隐藏文件）；
- 热键唤醒方式（双击空格 / `Alt+Space`）；
- 磁贴：每列卡片数、列间距、行间距；
- 桌面时钟开关；
- 动态壁纸：启用开关、壁纸列表与缩略图（带类型标签）、档位（原画/均衡/省电）、播放速度、声音开关与音量、全屏与锁屏自动暂停、手动暂停、导入文件/导入文件夹、更改存储位置、打开壁纸目录；不适用于当前壁纸类型的控件会置灰；
- 手动添加 / 移除应用；
- 一键恢复桌面图标。

## 快速开始

### 环境要求

- Windows 11（10.0.22631+，低版本未验证）
- Visual Studio 2022：C++ 桌面开发 + Windows 11 SDK + Windows App SDK（C++/WinRT）
- **WebView2 运行时**（资源管理器和网页壁纸都需要；Win11 通常已预装，缺失时这两项各自置灰，不影响其它功能）
- **Vulkan 运行时**（3D/着色器壁纸需要；显卡驱动自带，Windows 系统目录的 `vulkan-1.dll` + 一个可用显卡即可。本程序**不链接、不安装** Vulkan SDK，只在运行时动态加载）
- NuGet 包会在首次编译时自动还原（`packages.config`）

### 一键编译

```bat
build.bat        :: 编译 Release x64
build.bat test   :: 编译并运行单元测试
```

脚本会先自动关停正在运行的 Desktop Sticker 实例（避免文件占用），再执行还原与编译；程序仍在运行导致文件被锁是编译失败的最常见原因。

### 手动编译

1. 打开 `Desktop Sticker/Desktop Sticker.sln`
2. 选择 `x64` / `Release`，生成解决方案
3. 运行 `Desktop Sticker/x64/Release/Desktop Sticker/Desktop_Sticker.exe`

> `DesktopSticker.Features.dll` 与 `assets\weather\` 图标资源由构建事件自动复制到 EXE 目录。
> 程序为未打包 + 自包含模式，**无需单独安装 Windows App SDK Runtime**。

### 运行单元测试

```bat
build.bat test
:: 或直接运行：
Desktop Sticker\bin\x64\Release\Tests\DesktopSticker.Tests.exe
```

自研轻量测试框架（`dtest`），当前 **230 项**全部通过，覆盖配置存储、布局数学、图标分类、拼音检索、热键判定、时钟文案，以及动态壁纸的盘符选择、暂停优先级、档位解析、帧调度、调速余量累积、解码目标尺寸、主时钟选择、后端类型识别与自然排序、ffmpeg 命令行构造、JSON 存储容错与媒体库源文件安全，与 3D 几何（截角二十面体的 60 顶点 / 32 面 / Euler 数 / 共面与外向法线、通用凸包）与列主序矩阵（透视投影按 Vulkan 裁剪空间、右手系旋转），以及资源管理器的字节/百分比格式化、存储分类规则、模块归属判定、CPU 采样计算、JSON 响应组装。仅支持 Release（本机 Debug CRT 环境问题）。

## 使用说明

| 操作 | 效果 |
|---|---|
| 双击空格 | 唤起 / 聚焦搜索面板（输入状态不误触） |
| `Esc` | 关闭搜索面板 |
| 托盘左/右键 | 设置、恢复桌面、退出 |
| 双击桌面空白 | 切换干净桌面模式 |
| 拖动卡片空白处 | 移动分区（松手自动保存） |
| 拖动卡片边缘 | 缩放分区 |
| 双击分区标题 | 折叠 / 展开 |
| 滚轮（卡片上） | 平滑滚动磁贴 |
| 双击磁贴 | 打开对应文件 |
| 拖动磁贴 | 到其他分区=移动；到桌面空白=还原为桌面图标 |
| 右键磁贴 / 标题 | 打开、移出 / 重命名、删除分区 |
| 设置页「动态壁纸」 | 启用开关、导入文件/文件夹、切换壁纸、档位与速度、声音与音量、暂停规则、更改存储位置、打开壁纸目录 |
| 托盘「资源管理器」/ `--resmon` | 打开 CPU / 内存 / 存储 只读资源管理器窗口 |

> 首次启动会自动记录所有原生图标的原始位置并移入分区；退出程序时自动还原。`layout.json` 损坏时布局会自动重建，但图标原始位置记录不会丢失。

## 配置与数据

全部数据位于 `%APPDATA%\DesktopSticker\`：

| 文件 | 内容 |
|---|---|
| `config.json` | 热键方案、搜索范围、磁贴间距与每列卡片数、时钟开关等 |
| `layout.json` | 分区列表与位置、磁贴分类结果、原生图标原始位置（含版本号，自动迁移） |
| `apps.json` | 手动添加的应用列表 |
| `wallpaper.json` | 动态壁纸开关、当前壁纸、档位、暂停规则与存储位置记录（卷序列号 + 根目录文件 ID） |
| `debug.log` | 运行日志（按 `[workspace]` `[zone]` `[hotkey]` `[weather]` `[wallpaper]` 等来源标记，超 5MB 自动轮转） |

> 壁纸媒体库不在 `%APPDATA%`，而在首次启用时自动选定的固定盘上：`<盘>:\DesktopSticker\Wallpaper\`（含 `library.json` 与 `media\<id>\`）。

## 项目结构

```
├─ build.bat                          一键编译脚本
├─ Desktop Sticker/
│  ├─ Desktop Sticker.sln
│  ├─ Desktop Sticker/                WinUI 3 前端（托盘、启动器、设置页）
│  │  ├─ App/MainWindow/Host          应用入口、DLL 宿主、托盘窗口
│  │  ├─ LauncherController           搜索面板
│  │  ├─ SettingsController           设置页
│  │  └─ assets/weather/S2            QWeather S2 天气图标
│  ├─ DesktopSticker.Features/        功能层 DLL（Win32 + Direct2D，无 WinRT）
│  │  ├─ include/desktopsticker/      对外接口与组件头文件
│  │  ├─ src/                         分区、热键、检索、定位、图标等服务
│  │  └─ src/widgets/                 桌面小组件（时钟、天气服务）
│  ├─ DesktopSticker.Tests/           单元测试（dtest 框架）
│  ├─ DesktopSticker.WallPaper/       动态壁纸 DLL（D3D11 + DirectComposition + MF/FFmpeg/WIC/WebView2）
│  │  ├─ include/desktopsticker/      IWallPaperModule 接口与纯策略头（header-only）
│  │  └─ src/                         呈现、解码、媒体库、存储、暂停策略
│  ├─ DesktopSticker.ResMon/          资源管理器 DLL（WebView2 宿主 + CPU/内存/存储采集）
│  ├─ Desktop Sticker/resmon/         资源管理器前端（原生 HTML/CSS/JS，无框架无构建）
│  └─ bin/x64/Release/                构建输出
├─ third_party/nlohmann/json.hpp      唯一第三方依赖
└─ docs/acceptance.md                 手工验收清单
```

## 架构说明

- **EXE ↔ DLL 边界**：前端只通过 `IFeatureModule` 接口与 `CreateFeatureModule / DestroyFeatureModule` 导出函数使用功能层，其余头文件不对 EXE 暴露；两端必须由同一份源码同时编译（接口即 ABI）。
- **桌面嵌入**：分区与时钟窗口通过 `Progman / WorkerW / SHELLDLL_DefView` 嵌入桌面层（与 Wallpaper Engine 同思路的未文档化行为），失败时自动降级为置底普通窗口。
- **渲染**：分区与时钟使用「内存 DIB + Direct2D + UpdateLayeredWindow」管线实现半透明圆角卡片与逐帧动画。
- **鼠标输入**：低级鼠标钩子负责降级模式下的输入转发与"双击桌面空白"检测；嵌入模式下由系统直接命中子窗口。
- **线程模型**：UI 线程持有全部窗口与布局数据；热键钩子线程、目录监视线程、天气后台线程通过消息投递或加锁快照与 UI 线程交互。

## 图标版权

`assets/weather/S2/` 内的天气图标来自 [qwd/WeatherIcon](https://github.com/qwd/WeatherIcon)（和风天气），遵循 **CC BY 4.0** 许可，许可文本见 `assets/weather/LICENSE-CC-BY-4.0.txt`。

## 第三方组件与许可

完整的第三方声明见仓库根目录 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

- **FFmpeg**：动态壁纸以两种方式使用未经修改的 FFmpeg —— 以子进程方式运行 `ffmpeg.exe` 生成性能副本，以及通过公开 C 接口动态加载 `avformat` / `avcodec` / `avutil` / `swscale` 共享库作为解码兜底。**不静态链接任何 FFmpeg 库，也不向 Windows 注册系统解码器。** 负载为固定版本的 BtbN Windows x64 LGPL shared build，按 **GNU LGPL v3** 授权；许可正文见 `third_party/LICENSE-FFmpeg.txt`。
- **获取负载**：负载不入版本库，首次构建前运行 `tools/prepare_ffmpeg.ps1`（按锁定版本下载并校验 SHA-256）。缺负载时**编译仍可通过**，只是 FFmpeg 兜底解码与副本生成不可用。
- **OpenH264**：所固定的 FFmpeg 构建启用了 Cisco OpenH264 编码器（**BSD** 许可，见 `third_party/OpenH264-LICENSE.txt`）。这是第三方 FFmpeg 构建中集成的 OpenH264，**不是** Cisco 官方预编译二进制，本声明不主张 Cisco 对官方预编译二进制提供的专利许可适用于该构建。正式分发前，发布者仍需独立确认适用地区的 H.264 专利许可要求。
- **MotionWallpaper**：动态壁纸组件的实现方式参考并部分移植自 [MotionWallpaper](https://github.com/1114656/MotionWallpaper)（MIT，见 `third_party/MotionWallpaper-MIT.txt`）。

## 已知限制与排查

- 定位与天气依赖网络；IP 定位直连获取真实位置，若直连失败会自动回退代理线路，此时地区显示的是代理出口位置。
- 分区嵌入依赖 Explorer 桌面（`Progman`/`WorkerW`），嵌入失败会自动降级；重启 Explorer 后程序需重启以重新嵌入。
- 程序为单实例（重复启动自动退出）；热键在文本输入焦点时不触发，避免打字误触。
- 出现异常行为先看 `%APPDATA%\DesktopSticker\debug.log`，日志按来源标记、超 5MB 自动轮转。
