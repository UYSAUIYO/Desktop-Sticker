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

### 设置

Win11 系统设置风格界面（Mica 背景卡片、跟随系统深浅色），通过托盘菜单或 `--settings` 启动参数打开：

- 搜索范围（桌面 / 已知文件夹 / 开始菜单应用 / 隐藏文件）；
- 热键唤醒方式（双击空格 / `Alt+Space`）；
- 磁贴：每列卡片数、列间距、行间距；
- 桌面时钟开关；
- 手动添加 / 移除应用；
- 一键恢复桌面图标。

## 快速开始

### 环境要求

- Windows 11（10.0.22631+，低版本未验证）
- Visual Studio 2022：C++ 桌面开发 + Windows 11 SDK + Windows App SDK（C++/WinRT）
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

自研轻量测试框架（`dtest`），当前 **38 项**全部通过，覆盖配置存储、布局数学、图标分类、拼音检索、热键判定、时钟文案。仅支持 Release（本机 Debug CRT 环境问题）。

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

> 首次启动会自动记录所有原生图标的原始位置并移入分区；退出程序时自动还原。`layout.json` 损坏时布局会自动重建，但图标原始位置记录不会丢失。

## 配置与数据

全部数据位于 `%APPDATA%\DesktopSticker\`：

| 文件 | 内容 |
|---|---|
| `config.json` | 热键方案、搜索范围、磁贴间距与每列卡片数、时钟开关等 |
| `layout.json` | 分区列表与位置、磁贴分类结果、原生图标原始位置（含版本号，自动迁移） |
| `apps.json` | 手动添加的应用列表 |
| `debug.log` | 运行日志（按 `[workspace]` `[zone]` `[hotkey]` `[weather]` 等来源标记，超 5MB 自动轮转） |

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

## 已知限制与排查

- 定位与天气依赖网络；IP 定位直连获取真实位置，若直连失败会自动回退代理线路，此时地区显示的是代理出口位置。
- 分区嵌入依赖 Explorer 桌面（`Progman`/`WorkerW`），嵌入失败会自动降级；重启 Explorer 后程序需重启以重新嵌入。
- 程序为单实例（重复启动自动退出）；热键在文本输入焦点时不触发，避免打字误触。
- 出现异常行为先看 `%APPDATA%\DesktopSticker\debug.log`，日志按来源标记、超 5MB 自动轮转。
