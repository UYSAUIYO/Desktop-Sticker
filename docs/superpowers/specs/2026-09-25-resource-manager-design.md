# Desktop Sticker — 资源管理器（DesktopSticker.ResMon）设计文档

日期：2026-09-25
状态：设计已确认，待用户审阅
参考形态：ZCode Resource Manager（CPU / 内存 / 存储 三页签：总量 + 分类占比条 + 明细列表）

## 1. 背景与目标

为 Desktop Sticker 增加一个**资源管理器**窗口，用 Microsoft WebView2 承载前端界面，展示本程序自身的资源占用：

1. **CPU**：按线程列出 CPU 占用（本程序是单进程多线程，不是 ZCode 那种多进程架构），并单列子进程；
2. **内存**：按模块/映像列出加载占用，并给出进程级提交构成；
3. **存储**：按分类统计程序占用的磁盘空间，占比条 + 明细 + 重新计算。

价值：本程序会把桌面图标、壁纸媒体库（实测 352 MB）、FFmpeg 负载（实测 158 MB）等落到磁盘上，用户需要一个地方看清"到底占了多少、占在哪"，以及运行时的 CPU/内存分布。

非目标（明确排除，见第 10 节）：删除/清理动作、历史曲线与采样持久化、远程/多机监控、把第三方运行时的占用纳入"可回收"建议。

## 2. 已确认决策

| 项 | 决定 |
|---|---|
| CPU 口径 | 按**线程**（`GetThreadTimes` 差值采样）+ 子进程区 |
| 内存口径 | 按**模块/映像**（`EnumProcessModules`）+ 进程级提交构成 |
| 存储口径 | **只读统计**，占比条 + 明细 + 重新计算，无任何删除动作 |
| 窗口形态 | **独立纯 Win32 顶层窗口** + WebView2；托盘菜单打开 |
| 组件归属 | 新工程 `DesktopSticker.ResMon.dll`，独立接口，壁纸库路径由 EXE 注入 |
| 桥接方式 | `postMessage` + JSON 契约（不用 `AddHostObjectToScript`） |
| 刷新 | CPU/内存由 JS 侧 `setInterval` 每 2s 拉取；存储仅在点"重新计算"时扫描 |

### 环境现状（已核实）

- WebView2 运行时**已安装**：`153.0.4234.48`（HKLM EdgeUpdate Clients）。
- `Microsoft.Web.WebView2 1.0.3719.77` 已在 `packages.config` 中引用，NuGet 包提供 x64 `WebView2Loader.dll`，但**代码从未使用过 WebView2**（仓库内无 `ICoreWebView2` 引用）。

### 实测资源分布（分类口径的依据）

| 分类 | 实测占用 |
|---|---|
| 壁纸媒体库 | 352 MB（`E:\DesktopSticker\Wallpaper`） |
| FFmpeg 负载 | 158 MB（`<exeDir>\ffmpeg`） |
| 调试符号 | 约 72 MB（`<exeDir>\Desktop_Sticker.pdb`） |
| 天气图标资源 | 3.2 MB（`<exeDir>\assets`） |
| 配置与布局 | 约 28 KB（`config.json` + `layout.json` + `wallpaper.json`） |
| 日志 | 约 229 KB（`debug.log` + `debug.log.bak`） |

## 3. 组件与边界

### 3.1 新工程

`Desktop Sticker/DesktopSticker.ResMon/`，纯 C++20 Win32 DLL（允许 COM / WebView2，**不引入 WinRT/XAML**），与 `DesktopSticker.WallPaper` 同构：

```
include/desktopsticker/IResMonModule.h     对外唯一接口 + 导出
include/desktopsticker/ResMonExport.h      导出宏（文件名唯一，避免与 Features 的 Export.h 路径碰撞）
include/desktopsticker/resmon/
    Format.h        纯函数：字节/百分比格式化
    Classify.h      纯函数：路径 → 存储分类
    CpuMath.h       纯函数：两次采样 → CPU%
    JsonBuild.h     纯函数：JSON 字符串转义与响应组装
src/ResMonModule.cpp       IResMonModule 实现与导出工厂
src/ResMonWindow.cpp/.h    Win32 窗口与消息循环
src/WebViewHost.cpp/.h     WebView2 环境/控制器/虚拟主机映射/消息桥
src/ProcessSampler.cpp/.h  CPU（GetThreadTimes、子进程枚举）
src/MemorySampler.cpp/.h   内存（GetProcessMemoryInfo、EnumProcessModules）
src/StorageScanner.cpp/.h  存储分类扫描（工作线程）
```

### 3.2 EXE ↔ ResMon 边界

新建独立接口，**不复用** `IFeatureModule` / `IWallPaperModule`：

```cpp
// include/desktopsticker/IResMonModule.h
namespace desktopsticker {

// 由 EXE 注入，避免 DLL 之间相互依赖
struct ResMonPaths {
    std::wstring exeDir;         // 负载 / 资源 / PDB 所在目录
    std::wstring configDir;      // %APPDATA%\DesktopSticker
    std::wstring wallpaperRoot;  // 取自 IWallPaperModule::GetSettings().libraryRoot，可为空
};

class IResMonModule {
public:
    virtual ~IResMonModule() = default;
    virtual bool Init(const ResMonPaths& paths) = 0;  // WebView2 环境创建；失败返回 false
    virtual bool Show() = 0;                          // 创建或前置窗口
    virtual void Shutdown() = 0;
    virtual bool Available() = 0;                     // 环境是否可用（托盘项据此置灰）
};

} // namespace desktopsticker

extern "C" DESKTOPSTICKER_RESMON_API desktopsticker::IResMonModule* CreateResMonModule();
extern "C" DESKTOPSTICKER_RESMON_API void DestroyResMonModule(desktopsticker::IResMonModule*);
```

`Host` 增加第三条并行加载路径（`LoadResMon()` / `UnloadResMon()`），**不做泛型抽象**（三个 DLL 仍各写各的）。

### 3.3 降级要求

DLL 缺失、`LoadLibrary` 失败、导出缺失、`Init` 返回 false（WebView2 运行时缺失或环境创建失败）→ 一律降级为"资源管理器不可用"：托盘菜单项置灰、`debug.log` 记录，**不得影响分区收纳、搜索启动器、桌面时钟、动态壁纸**。异常不得穿透到 `OnLaunched`。

`Init` 失败时 EXE **保留模块实例**（不当作加载失败丢弃），以便托盘菜单项以"置灰"而非"消失"的方式呈现，用户能明确看到该功能存在但不可用。

## 4. 窗口与 WebView2 宿主

### 4.1 窗口

- 纯 Win32 顶层窗口：`WS_OVERLAPPEDWINDOW`（可缩放），初始约 920×640（按系统 DPI 换算为物理像素），**不使用 WinUI/XAML**。
  > 选择理由：WebView2 本身就是 HWND 宿主；用 Win32 窗口可完全避开 `AGENTS.md` 记载的 WinUI 次级窗口脆弱点（点标题栏 X 会销毁 XAML `Window`，必须订阅 `Closed` 并整体重建，`Show`/`Hide` 必须 try/catch）。
- 托盘菜单新增"资源管理器"项；窗口已存在则前置而不是重复创建。
- 关闭窗口即销毁 WebView2 控制器与窗口；再次打开重建。
- DPI：遵循进程清单设置；窗口尺寸用 `GetDpiForWindow` 换算。

### 4.2 WebView2

- `CreateCoreWebView2EnvironmentWithOptions`，user data folder 固定为 `%LOCALAPPDATA%\DesktopSticker\ResMonWebView`（**不写程序目录**，卸载可直接删）。
- `CreateCoreWebView2Controller(hwnd)` → `get_CoreWebView2`。
- `SetVirtualHostNameToFolderMapping(L"resmon.local", <exeDir>\resmon, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW)` → 导航 `https://resmon.local/index.html`。
  > 用虚拟主机映射而非 `file://`：避免本地文件访问限制，也让相对路径的 JS/CSS 正常加载。
- `add_WebMessageReceived` 接收 JS 命令；`PostWebMessageAsJson` 回传响应。
- 关窗时按序释放：controller → environment → 窗口。

### 4.3 前端资源

- 仓库内 `Desktop Sticker/Desktop Sticker/resmon/`：`index.html`、`app.js`、`style.css`。
- `PostBuildEvent` 复制到 `<exeDir>\resmon\`（与 `assets\weather` 同一套做法）。
- 前端只做展示与轮询：三页签（CPU / 内存 / 存储）、总量卡片、占比条、明细列表、"重新计算"按钮。
- **不引入任何前端框架与构建步骤**（无 npm、无打包）：原生 HTML + CSS + JS，与仓库"无 cmake、无 globbing"的极简构建风格一致。

### 4.4 构建注意

`WebView2Loader.dll` 必须与 EXE 同目录。NuGet 的 `.targets` 通常会处理，但 `PostBuildEvent` 中**显式 xcopy 兜底**（缺失时不让构建失败，资源管理器降级即可）。

## 5. JS ↔ 原生桥

### 5.1 契约

```text
JS → 原生（PostWebMessageAsJson）
  { "cmd": "cpu" }
  { "cmd": "memory" }
  { "cmd": "storage" }        // 触发一次扫描（重操作）

原生 → JS
  { "type": "cpu", "sampledAtMs": 0, "totalPercent": 0.0,
    "threads": [ { "tid": 0, "name": "", "cpuPercent": 0.0, "totalMs": 0 } ],
    "children": [ { "pid": 0, "name": "", "cpuPercent": 0.0, "workingSetBytes": 0 } ],
    "baseline": true }

  { "type": "memory", "workingSetBytes": 0, "privateBytes": 0, "virtualBytes": 0,
    "modules": [ { "name": "", "imageBytes": 0 } ],
    "children": [ { "pid": 0, "name": "", "workingSetBytes": 0 } ] }

  { "type": "storage", "scannedAtMs": 0, "totalBytes": 0, "accountedBytes": 0,
    "categories": [ { "id": "", "name": "", "bytes": 0, "percent": 0.0, "color": "", "note": "" } ],
    "warnings": [ "" ] }

  { "type": "error", "cmd": "", "message": "" }
```

### 5.2 为什么用 JSON 而非 `AddHostObjectToScript`

`AddHostObjectToScript` 需要在无 WinRT 的 C++ 里手写 `IDispatch`，成本高且难测；而 JSON 契约的**组装与转义是纯函数，可以直接用 dtest 单测**。这是选择它的主要动机，不是为了省事。契约一旦定下就是两端共用的接口，故写进本节。

### 5.3 刷新与并发

- CPU / 内存：JS 侧 `setInterval(2000)` 发送 `cmd`；页面不可见（`document.hidden`）时暂停轮询。
- 存储：只在点"重新计算"时发送 `cmd:"storage"`；扫描在**工作线程**执行。前端在发出请求后立即自行切换为"正在计算…"状态（原生不回传进度，避免多一种消息类型）。
- 同一命令重入时按递增请求号丢弃旧响应，避免慢扫描的结果覆盖新结果。
- **所有采样与扫描都在工作线程**，不得阻塞 EXE 的 UI 线程（与既有"UI 线程持有全部窗口与布局数据"的约定一致）。

## 6. 三条采集路径

### 6.1 CPU（按线程）

- 线程枚举：`CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)` 过滤 `th32OwnerProcessID == GetCurrentProcessId()`。
- 每线程：`OpenThread(THREAD_QUERY_LIMITED_INFORMATION)` → `GetThreadTimes` 取内核+用户时间；与上次采样求差，除以两次采样的 `QueryPerformanceCounter` 墙钟差得百分比。
- 线程名：`GetThreadDescription`（Win10 1607+）。**为了让列表可读**，需在 Features 与 WallPaper 的线程创建处补 `SetThreadDescription`：
  - Features：UI/分区渲染、热键钩子、目录监视、天气后台；
  - WallPaper：渲染与帧调度、暂停监控、转码工作线程。
  > 这是**纯附加、零行为改动**（仅设置调试用的线程描述），不触碰任何既有逻辑。未命名的线程显示为"线程 &lt;tid&gt;"。
- 子进程：`CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` 过滤 `th32ParentProcessID == GetCurrentProcessId()`（即转码时的 `ffmpeg.exe`），用 `GetProcessTimes` / `GetProcessMemoryInfo` 取值。
- 首次采样无基线 → `baseline: true`，百分比返回 0，前端标注"正在建立基线"而不是显示垃圾数字。

### 6.2 内存（按模块）

- 进程级：`GetProcessMemoryInfo`（`PROCESS_MEMORY_COUNTERS_EX`）取 `WorkingSetSize`（工作集）、`PrivateUsage`（私有提交）、`PeakWorkingSetSize`（峰值工作集）。
  > 不报"虚拟大小"：PSAPI 不提供该字段，`NtQueryInformationProcess` 属未文档化接口，为一个展示项引入不值得。
- 模块列表：`EnumProcessModules` + `GetModuleInformation` → 模块名与 `SizeOfImage`，按映像大小降序。
- 子进程：同 6.1。
- **不使用** `PERFORMANCE_INFORMATION` 之类的全系统接口：只关心本程序。

### 6.3 存储（分类统计）

扫描根：`exeDir`、`configDir`，以及（若非空）`wallpaperRoot`。分类采用**有序规则、首个命中即定**（等效于最长匹配优先），保证每个路径恰好归入一类：

| 顺序 | id | 名称 | 匹配规则 | 备注 |
|---|---|---|---|---|
| 1 | `wallpaper` | 壁纸媒体库 | `wallpaperRoot\` 下全部内容（含 `library.json`） | 实测 352 MB |
| 2 | `ffmpeg` | FFmpeg 负载 | `exeDir\ffmpeg\` 下全部内容 | 实测 158 MB |
| 3 | `assets` | 天气图标资源 | `exeDir\assets\` 下全部内容 | 实测 3.2 MB |
| 4 | `pdb` | 调试符号 | `exeDir` 直接子文件且扩展名为 `.pdb` | 标注"可安全删除（仅影响调试）" |
| 5 | `program` | 程序主体 | `exeDir` 直接子文件中的 `Desktop_Sticker.exe`、`DesktopSticker.Features.dll`、`DesktopSticker.WallPaper.dll`、`DesktopSticker.ResMon.dll` | 不含 PDB |
| 6 | `runtime` | 运行时与框架 | `exeDir` 下**其余全部内容**（Windows App SDK / onnxruntime / DirectML / DirectX 等 DLL、`resmon\` 前端资源、其余子目录） | **只读，标注"不建议删除"** |
| 7 | `config` | 配置与布局 | `configDir\config.json`、`apps.json`、`layout.json`、`wallpaper.json` | 约 28 KB |
| 8 | `logs` | 日志 | `configDir\debug.log`、`configDir\*.bak` | 约 229 KB |
| 9 | `other` | 其他 | `configDir\` 下上述未命中的内容 | 兜底 |

要求：
- 规则按**路径分量**匹配，不做裸字符串前缀比较：`ffmpeg-sdk` 不得被 `ffmpeg` 规则命中。
- 比较大小写不敏感（Windows 语义）。
- `totalBytes` = 所有分类之和；`percent` 由纯函数计算，**舍入后以最大类补差，保证合计恰为 100.0**（见 7.1）。
- 主扫描目标为**文件**（`resmon\` 前端资源归入 `runtime`，因其体量在 10^4 字节量级、单列无信息价值）。空目录计入 0 字节。
- 扫描遇到不存在/无权限/被占用的路径 → 跳过并写入 `warnings`，**页面上明示，不静默、不崩溃**。
- 扫描只读：不创建、不修改、不删除任何文件。
- `wallpaperRoot` 为空（壁纸模块不可用）时跳过规则 1 并在 `warnings` 说明。

## 7. 纯函数与测试

沿用 `dtest` 与 WallPaper 的做法：把逻辑提成 header-only 纯函数再测。既有 **87 项必须保持全绿**。

### 7.1 待测纯函数

| 模块 | 覆盖点 |
|---|---|
| `Format.h` | 字节格式化（B/KB/MB/GB，1024 进制，保留 1 位小数；与参考图 `4 KB` / `54.3 GB` 风格一致）；0 与极大值 |
| `Classify.h` | 路径 → 分类：**最长匹配优先**；边界含 exe 目录自身、壁纸库在别的盘、前缀相似但不同的目录（如 `ffmpeg-sdk` 不得归入 `ffmpeg`）、大小写不敏感 |
| `CpuMath.h` | 两次采样 → 百分比：正常、首次无基线（返回 0 且标 baseline）、墙钟差为 0（除零保护）、线程中途消失 |
| `JsonBuild.h` | JSON 字符串转义：反斜杠（Windows 路径必然出现）、中文、引号、控制字符；数组/对象组装合法性 |
| 百分比归并 | 总计为 0、单类独占、舍入后合计不为 100% 的处理（明确策略：以最大类补差，保证合计为 100.0） |

### 7.2 不做单测的部分

窗口、WebView2 宿主、系统采样（`GetThreadTimes`/`GetProcessMemoryInfo`/目录扫描）依赖真实系统状态，走人工验收；采样器只做薄封装，逻辑尽量下沉到 7.1 的纯函数。

## 8. 错误处理与降级

| 情形 | 行为 |
|---|---|
| WebView2 运行时缺失 / 环境创建失败 | `Init` 返回 false → `Available()==false`，托盘项置灰，记录日志，其余功能不受影响 |
| `resmon\index.html` 缺失 | 显示内置错误页（原生侧导航失败时注入），不留白屏 |
| 前端资源部分缺失（CSS/JS） | 页面可加载但样式/交互缺失，`warnings` 中记录 |
| 扫描目录不存在/无权限/被占用 | 跳过该类并写入 `warnings`，页面明示 |
| 采样期间目标线程/进程消失 | 跳过该项，不视为错误 |
| 窗口关闭再打开 | 销毁并重建 controller 与窗口；不残留窗口或进程 |
| `ResMon.dll` 改名/缺失 | 托盘项置灰，分区/搜索/时钟/壁纸全部正常 |

## 9. 人工验收

- 托盘菜单出现"资源管理器"，打开后三页签与参考图形态一致（总量卡片 + 占比条 + 明细列表）
- CPU 页每 2s 刷新，数值随实际负载变化；线程名可读（非 `线程 <tid>`）
- 触发一次壁纸转码（导入大视频）→ CPU 页出现 `ffmpeg.exe` 子进程项
- 内存页列出模块映像大小并降序；总量与任务管理器同量级
- 存储页"重新计算"后：**壁纸库 ≈ 352 MB、FFmpeg 负载 ≈ 158 MB**，合计与各分类之和一致
- 存储页把某分类目录改名制造缺失 → 出现 `warnings` 提示且页面不崩
- 关闭窗口再打开正常；关闭后 `tools/window_visibility.ps1` 无残留窗口
- 把 `DesktopSticker.ResMon.dll` 改名 → 托盘项置灰，其余功能全正常
- 分区拖拽/滚轮/双击、搜索启动器、桌面时钟、动态壁纸全部无回归
- 构建仍为 Release x64 全绿，dtest 87 + 新增全通过

## 10. 未纳入范围

- 任何删除/清理动作（含"一键清理日志""删除壁纸副本"）——只读统计
- 历史曲线、采样持久化、导出报告
- 远程/多机监控
- 把第三方运行时（Windows App SDK / onnxruntime / DirectML）标为可回收
- GPU 占用与磁盘 IO 统计
- 前端框架、npm 构建链

## 11. 风险与对策

| 风险 | 对策 |
|---|---|
| WebView2 在不同机器上运行时缺失 | `Init` 失败即降级；不打包运行时（Evergreen），仅在日志与页面提示 |
| `WebView2Loader.dll` 未被复制到 EXE 旁 | `PostBuildEvent` 显式 xcopy 兜底；缺失只降级不失败构建 |
| 存储扫描在大壁纸库上耗时 | 工作线程扫描 + 前端显示进行中状态；只在用户点击时触发 |
| 目录扫描跨盘（壁纸库在 E:） | 分类规则按绝对路径匹配，不假设同盘 |
| 线程 CPU 采样精度受 100ns 分辨率限制 | 2s 采样窗口足够；极短命线程可能漏采，属已知限制 |
| `SetThreadDescription` 改动 Features 线程创建处 | 仅新增一行调用，不改线程行为；人工验收覆盖分区/时钟无回归 |
