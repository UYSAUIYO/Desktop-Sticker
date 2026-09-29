# Desktop Sticker 开发文档

本文说明 Desktop Sticker 的构建、运行、测试、项目结构与架构。软件功能与组件许可见 [README.md](README.md)（English: [README.en.md](README.en.md)）。

## 环境要求

- Windows 11（10.0.22631+，低版本未验证）
- Visual Studio 2022：C++ 桌面开发 + Windows 11 SDK + Windows App SDK（C++/WinRT）
- **WebView2 运行时**（资源管理器和网页壁纸都需要；Win11 通常已预装，缺失时这两项各自置灰，不影响其它功能）
- **Vulkan 运行时**（3D/着色器壁纸需要；显卡驱动自带，Windows 系统目录的 `vulkan-1.dll` + 一个可用显卡即可。本程序**不链接、不安装** Vulkan SDK，只在运行时动态加载）
- NuGet 包会在首次编译时自动还原（`packages.config`）

## 一键编译

```bat
build.bat        :: 编译 Release x64
build.bat test   :: 编译并运行单元测试
```

脚本会先自动关停正在运行的 Desktop Sticker 实例（避免文件占用），再执行还原与编译；程序仍在运行导致文件被锁是编译失败的最常见原因。

## 手动编译

1. 打开 `Desktop Sticker/Desktop Sticker.sln`
2. 选择 `x64` / `Release`，生成解决方案
3. 运行 `Desktop Sticker/x64/Release/Desktop Sticker/Desktop_Sticker.exe`

> `DesktopSticker.Features.dll` 与 `assets\weather\` 图标资源由构建事件自动复制到 EXE 目录。
> 程序为未打包 + 自包含模式，**无需单独安装 Windows App SDK Runtime**。

## 第三方负载准备（可选）

以下负载不入版本库，缺失时编译仍可通过，只是对应功能降级：

- **FFmpeg**：首次构建前运行 `tools/prepare_ffmpeg.ps1`（按锁定版本下载并校验 SHA-256）。缺负载时 FFmpeg 兜底解码与性能副本生成不可用。
- **Vulkan / shaderc**：`tools/prepare_vulkan.ps1`、`tools/prepare_shaderc.ps1`（仅构建/测试 ④ Vulkan 后端需要）。
- **Godot 运行库 / 编辑器**：`tools/build_godot.ps1`（默认出运行库；`-Target Editor` 出编辑器）。缺运行库时场景壁纸报不可用，其余后端不受影响。

## 运行单元测试

```bat
build.bat test
:: 或直接运行：
Desktop Sticker\bin\x64\Release\Tests\DesktopSticker.Tests.exe
```

自研轻量测试框架（`dtest`），覆盖配置存储、布局数学、图标分类、拼音检索、热键判定、时钟文案，动态壁纸的盘符选择、暂停优先级、档位解析、帧调度、调速余量累积、解码目标尺寸、主时钟选择、后端类型识别与自然排序、ffmpeg 命令行构造、JSON 存储容错与媒体库源文件安全、`.dswall` 清单解析，3D 几何（截角二十面体与通用凸包）与列主序矩阵，以及资源管理器的字节/百分比格式化、存储分类规则、模块归属判定、CPU 采样计算、JSON 响应组装。仅支持 Release（本机 Debug CRT 环境问题）。

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
| 托盘「资源管理器」 | 打开 CPU / 内存 / 存储 只读资源管理器窗口 |

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
│  ├─ DesktopSticker.WallPaper/       动态壁纸 DLL（D3D11 + DirectComposition + MF/FFmpeg/WIC/WebView2/Vulkan/Godot）
│  │  ├─ include/desktopsticker/      IWallPaperModule 接口与纯策略头（header-only）
│  │  └─ src/                         呈现、解码、媒体库、存储、暂停策略
│  ├─ DesktopSticker.ResMon/          资源管理器 DLL（WebView2 宿主 + CPU/内存/存储采集）
│  ├─ Desktop Sticker/resmon/         资源管理器前端（原生 HTML/CSS/JS，无框架无构建）
│  └─ bin/x64/Release/                构建输出
├─ tools/                             构建与验证脚本、Godot fork（godot-src）、FFmpeg/Vulkan 负载准备
├─ third_party/nlohmann/json.hpp      唯一随仓库分发的第三方依赖
└─ docs/acceptance.md                 手工验收清单
```

## 架构说明

- **EXE ↔ DLL 边界**：前端只通过 `IFeatureModule` 接口与 `CreateFeatureModule / DestroyFeatureModule` 导出函数使用功能层，其余头文件不对 EXE 暴露；两端必须由同一份源码同时编译（接口即 ABI）。WallPaper、ResMon 各自有独立的模块接口与导出函数，作为可选模块被宿主加载。
- **桌面嵌入**：分区与时钟窗口通过 `Progman / WorkerW / SHELLDLL_DefView` 嵌入桌面层（与 Wallpaper Engine 同思路的未文档化行为），失败时自动降级为置底普通窗口。
- **渲染**：分区与时钟使用「内存 DIB + Direct2D + UpdateLayeredWindow」管线实现半透明圆角卡片与逐帧动画。
- **动态壁纸**：`IWallpaperBackend` 背后五类后端（视频 / 图片序列 / 网页 / Vulkan 着色器 / Godot 场景）；几何与策略逻辑放在 header-only 纯函数里以便 `dtest` 覆盖，OS 适配放 `src/`。场景壁纸以内嵌 Godot 运行库在进程内渲染（libgodot 为进程级单例）。
- **鼠标输入**：低级鼠标钩子负责降级模式下的输入转发与"双击桌面空白"检测；嵌入模式下由系统直接命中子窗口。
- **线程模型**：UI 线程持有全部窗口与布局数据；热键钩子线程、目录监视线程、天气后台线程通过消息投递或加锁快照与 UI 线程交互。

## 已知限制与排查

- 定位与天气依赖网络；IP 定位直连获取真实位置，若直连失败会自动回退代理线路，此时地区显示的是代理出口位置。
- 分区嵌入依赖 Explorer 桌面（`Progman`/`WorkerW`），嵌入失败会自动降级；重启 Explorer 后程序需重启以重新嵌入。
- 程序为单实例（重复启动自动退出）；热键在文本输入焦点时不触发，避免打字误触。
- 出现异常行为先看 `%APPDATA%\DesktopSticker\debug.log`，日志按来源标记、超 5MB 自动轮转。
