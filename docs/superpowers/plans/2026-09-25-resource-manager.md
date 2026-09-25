# 资源管理器（DesktopSticker.ResMon）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Desktop Sticker 增加一个 WebView2 承载的只读资源管理器窗口：CPU 按线程、内存按模块、存储按 9 类有序规则统计。

**Architecture:** 新增 `DesktopSticker.ResMon.dll`（纯 C++20 Win32，允许 COM/WebView2，不引入 WinRT/XAML），自带独立接口 `IResMonModule`；EXE 作为第三个可选模块加载。窗口是纯 Win32 顶层窗口，WebView2 用虚拟主机映射指向 `<exeDir>\resmon` 下的原生 HTML/CSS/JS；两端用 `postMessage` + JSON 通信。格式/分类/CPU 计算/响应组装写成 header-only 纯函数，由 `dtest` 覆盖；系统调用（`GetThreadTimes`/`GetProcessMemoryInfo`/目录扫描）只做薄封装。

**Tech Stack:** C++20 / MSVC v143 / MSBuild（无 cmake）、Win32、WebView2（`Microsoft.Web.WebView2 1.0.3719.77`，已在 `packages.config`）、PSAPI、Toolhelp32、nlohmann/json、dtest。

**Spec:** `docs/superpowers/specs/2026-09-25-resource-manager-design.md`

## Global Constraints

- 构建**仅 Release x64**：`build.bat`（或 `build.bat test`）。本机 Debug CRT 异常，一律不动 Debug。
- 无 cmake、无 globbing：**每个新增源文件/头文件都必须手动写进 `.vcxproj` 与 `.filters`**。
- 既有测试基线：**87 passed, 0 failed**。每个任务后只能增加，不得回退。
- 运行测试：`cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && cp -f DesktopSticker.Features.dll Tests/ && cp -f DesktopSticker.WallPaper.dll Tests/ && cp -f DesktopSticker.ResMon.dll Tests/ && ./Tests/DesktopSticker.Tests.exe`
- 提交信息：小写英文 `feat:` / `fix:` / `docs:` / `test:` 单行。
- 标识符英文、注释与 UI 文案中文。
- 导出宏头文件**必须叫 `ResMonExport.h`**，不得叫 `Export.h`：EXE 同时包含多个 DLL 的头，相同相对路径会被 `#pragma once` 跳过（WallPaper 已踩过此坑）。
- **降级是硬要求**：ResMon 缺失/加载失败/`Init` 失败，一律只让托盘菜单项置灰，**不得影响分区收纳、搜索启动器、桌面时钟、动态壁纸**。托盘回调里的任何异常都必须被 try/catch 吞掉（运行在原生窗口过程内，异常会直接闪退进程）。
- **只读**：资源管理器不得创建、修改、删除任何文件。
- 采样与扫描全部在**工作线程**，不得阻塞 EXE 的 UI 线程。
- 每个任务结束后既有 87 项必须全绿。

## 文件结构总览

新增工程 `Desktop Sticker/DesktopSticker.ResMon/`：

| 路径 | 职责 |
|---|---|
| `DesktopSticker.ResMon.vcxproj` / `.filters` | 新工程；`DynamicLibrary`，OutDir 同 Features |
| `pch.h` / `pch.cpp` | 预编译头（windows.h、psapi、tlhelp32、WebView2.h、nlohmann/json） |
| `include/desktopsticker/ResMonExport.h` | `DESKTOPSTICKER_RESMON_API` 宏（文件名唯一，见约束） |
| `include/desktopsticker/IResMonModule.h` | 对外唯一接口 + `ResMonPaths` + extern "C" 导出 |
| `include/desktopsticker/resmon/Format.h` | **纯函数**：字节格式化、百分比与最大类补差 |
| `include/desktopsticker/resmon/Classify.h` | **纯函数**：路径 → 9 类存储分类（有序规则） |
| `include/desktopsticker/resmon/CpuMath.h` | **纯函数**：两次采样 → CPU% |
| `include/desktopsticker/resmon/JsonBuild.h` | **纯函数**：快照结构体 + `to_utf8` + 响应组装（nlohmann） |
| `src/ProcessSampler.cpp/.h` | `GetThreadTimes` 按线程 + 子进程枚举 → `CpuSnapshot` |
| `src/MemorySampler.cpp/.h` | `GetProcessMemoryInfo` + `EnumProcessModules` → `MemorySnapshot` |
| `src/StorageScanner.cpp/.h` | 9 类扫描 → `StorageSnapshot`（工作线程） |
| `src/ResMonWindow.cpp/.h` | 纯 Win32 顶层窗口、DPI、单实例前置 |
| `src/WebViewHost.cpp/.h` | WebView2 环境/控制器/虚拟主机映射/消息桥与命令分发 |
| `src/ResMonModule.cpp` | `IResMonModule` 实现与导出工厂 |

新增前端资源 `Desktop Sticker/Desktop Sticker/resmon/`：`index.html`、`style.css`、`app.js`。

修改既有文件：

| 路径 | 改动 |
|---|---|
| `Desktop Sticker/Desktop Sticker.sln` | 加入新工程与配置映射 |
| `Desktop Sticker/Desktop Sticker/Desktop Sticker.vcxproj` | `ProjectReference` 新工程；include 目录；`PostBuildEvent` 复制 ResMon DLL 与 `WebView2Loader.dll` |
| `Desktop Sticker/Desktop Sticker/Host.h` / `Host.cpp` | 第三条并行加载路径 `LoadResMon`/`UnloadResMon` |
| `Desktop Sticker/Desktop Sticker/MainWindow.xaml.cpp` | 托盘菜单加"资源管理器"项（不可用置灰） |
| `Desktop Sticker/Desktop Sticker/App.xaml.cpp` | 构造 `ResMonPaths` 并加载；命名 UI 线程 |
| `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj` | 加 ResMon include 目录、链接 lib、新测试文件 |
| `DesktopSticker.Features/src/DirectoryWatcher.cpp:22`、`HotkeyService.cpp:106`、`widgets/WeatherService.cpp:38` | 线程命名（各 +1 行） |
| `DesktopSticker.WallPaper/src/FrameSchedulerLoop.cpp:36`、`WallPaperModule.cpp:525`、`WallPaperModule.cpp:550` | 线程命名（各 +1 行） |
| `README.md` / `AGENTS.md` / `docs/acceptance.md` | 新组件、入口、验收项 |

---

## Phase A — 骨架与宿主接线

### Task 1: ResMon 工程骨架与对外接口

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/DesktopSticker.ResMon.vcxproj`（+ `.filters`）
- Create: `Desktop Sticker/DesktopSticker.ResMon/pch.h`、`pch.cpp`
- Create: `.../include/desktopsticker/ResMonExport.h`
- Create: `.../include/desktopsticker/IResMonModule.h`
- Create: `.../src/ResMonModule.cpp`
- Modify: `Desktop Sticker/Desktop Sticker.sln`
- Modify: `Desktop Sticker/Desktop Sticker/Desktop Sticker.vcxproj`

**Interfaces:**
- Consumes: 无
- Produces:
  - `desktopsticker::ResMonPaths { std::wstring exeDir, configDir, wallpaperRoot; }`
  - `desktopsticker::IResMonModule`：`bool Init(const ResMonPaths&)`、`bool Show()`、`void Shutdown()`、`bool Available()`
  - 导出 `CreateResMonModule` / `DestroyResMonModule`

- [ ] **Step 1: 写 `pch.h`**

```cpp
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <wrl/client.h>          // WebView2.h 依赖 WRL ComPtr
#include "WebView2.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <vector>
```

`pch.cpp`：`#include "pch.h"`

- [ ] **Step 2: 写 `ResMonExport.h`**

```cpp
#pragma once

// 文件名刻意不叫 Export.h：EXE 同时包含多个 DLL 的头，相同相对路径会被
// #pragma once 跳过，导致后一个项目的宏永不定义（WallPaper 已踩过此坑）。
#ifdef DESKTOPSTICKER_RESMON_BUILD
#define DESKTOPSTICKER_RESMON_API __declspec(dllexport)
#else
#define DESKTOPSTICKER_RESMON_API __declspec(dllimport)
#endif
```

- [ ] **Step 3: 写 `IResMonModule.h`**

严格按规格 3.2。注意本头文件**不得** include pch.h / WebView2.h / nlohmann，必须能被 EXE 与测试工程独立包含：

```cpp
#pragma once

#include <string>

#include "desktopsticker/ResMonExport.h"

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
    // WebView2 环境创建；失败返回 false（模块实例仍保留，供 Available() 查询后置灰菜单）
    virtual bool Init(const ResMonPaths& paths) = 0;
    virtual bool Show() = 0;      // 创建或前置窗口
    virtual void Shutdown() = 0;
    virtual bool Available() = 0; // 环境是否可用
};

} // namespace desktopsticker

extern "C" DESKTOPSTICKER_RESMON_API desktopsticker::IResMonModule* CreateResMonModule();
extern "C" DESKTOPSTICKER_RESMON_API void DestroyResMonModule(desktopsticker::IResMonModule*);
```

- [ ] **Step 4: 写 `src/ResMonModule.cpp` 最小实现**

骨架：`Init` 存 paths 并返回 `false`（本任务还没有 WebView2，先降级）、`Show` 返回 false、`Shutdown` 空、`Available` 返回 false。工厂 `new`/`delete` + try/catch。

- [ ] **Step 5: 写 vcxproj**

照 `DesktopSticker.WallPaper.vcxproj` 同构：
- `ConfigurationType = DynamicLibrary`、`TargetName = DesktopSticker.ResMon`
- GUID `{44444444-5555-6666-7777-888888888888}`
- `OutDir = $(SolutionDir)bin\$(Platform)\$(Configuration)\`
- `IntDir = $(SolutionDir)obj\$(Platform)\$(Configuration)\ResMon\`
- 预处理宏 `DESKTOPSTICKER_RESMON_BUILD`
- include 目录：`$(ProjectDir);$(ProjectDir)include;$(ProjectDir)src;$(ProjectDir)..\..\third_party;$(ProjectDir)..\packages\Microsoft.Web.WebView2.1.0.3719.77\build\native\include`
- **导入 WebView2 targets**（它会把 `WebView2Loader.dll.lib` 自动加进 `AdditionalDependencies`、并把 `WebView2Loader.dll` 复制到本项目 OutDir）：

```xml
<Import Project="$(ProjectDir)..\packages\Microsoft.Web.WebView2.1.0.3719.77\build\native\Microsoft.Web.WebView2.targets"
        Condition="Exists('$(ProjectDir)..\packages\Microsoft.Web.WebView2.1.0.3719.77\build\native\Microsoft.Web.WebView2.targets')" />
```
- `AdditionalDependencies`：`psapi.lib;version.lib;ole32.lib;shlwapi.lib;shell32.lib;uuid.lib`
- `LanguageStandard = stdcpp20`，`PrecompiledHeader = Use` / `pch.h`
- 显式列出 `pch.cpp`、`src\ResMonModule.cpp` 与全部 `ClInclude`
- 手动维护 `.vcxproj.filters`

- [ ] **Step 6: 入解决方案、EXE 引用与构建事件**

```
sln：加入 DesktopSticker.ResMon 工程，补齐 Release|x64 的 .Build.0 与各配置映射
     （照 WallPaper 的 6 条映射写法：ARM64/x64/x86 都映射到 x64）。
EXE vcxproj：
  - <ProjectReference Include="..\DesktopSticker.ResMon\DesktopSticker.ResMon.vcxproj"> 带 <LinkLibraryDependencies>false</LinkLibraryDependencies>
  - AdditionalIncludeDirectories 追加 $(ProjectDir)..\DesktopSticker.ResMon\include;
  - PostBuildEvent 追加两条（WebView2 的 targets 只复制到 ResMon 项目的 OutDir，
    而 EXE 从自己的目录加载 DLL，所以必须再往 EXE 目录复制一次）：
      xcopy /Y /D "$(SolutionDir)bin\$(Platform)\$(Configuration)\DesktopSticker.ResMon.dll" "$(OutDir)"
      xcopy /Y /D "$(SolutionDir)bin\$(Platform)\$(Configuration)\WebView2Loader.dll" "$(OutDir)"
```

- [ ] **Step 7: 编译验证**

```bash
cd "D:/project/Desktop Sticker"
cmd //c build.bat
ls "Desktop Sticker/bin/x64/Release/DesktopSticker.ResMon.dll"
ls "Desktop Sticker/bin/x64/Release/WebView2Loader.dll"
ls "Desktop Sticker/x64/Release/Desktop Sticker/DesktopSticker.ResMon.dll"
ls "Desktop Sticker/x64/Release/Desktop Sticker/WebView2Loader.dll"
```

- [ ] **Step 8: 跑既有测试确认无回归（87 passed）**

- [ ] **Step 9: Commit**

```bash
git commit -m "feat(resmon): scaffold DesktopSticker.ResMon dll with IResMonModule interface"
```

---

### Task 2: Host 第三条加载路径与托盘菜单入口

**Files:**
- Modify: `Desktop Sticker/Desktop Sticker/Host.h`、`Host.cpp`
- Modify: `Desktop Sticker/Desktop Sticker/App.xaml.cpp`
- Modify: `Desktop Sticker/Desktop Sticker/MainWindow.xaml.cpp:82-108`

**Interfaces:**
- Consumes: Task 1 的 `IResMonModule.h`
- Produces: `Host::LoadResMon()`、`Host::UnloadResMon()`、`Host::ResMon()`（可能为 nullptr）

- [ ] **Step 1: 扩展 `Host.h`**

新增 `HMODULE rmDll_`、`desktopsticker::IResMonModule* rmModule_`，以及：

```cpp
bool LoadResMon();
void UnloadResMon();
desktopsticker::IResMonModule* ResMon() const { return rmModule_; }
```

- [ ] **Step 2: 实现 `LoadResMon` / `UnloadResMon`**

照 `LoadWallPaper` 同构（`Host.cpp`）：
- 从 EXE 目录加载 `DesktopSticker.ResMon.dll`，`GetProcAddress` 取 `CreateResMonModule` / `DestroyResMonModule`
- 构造 `ResMonPaths`：`exeDir` = EXE 目录；`configDir` = `%APPDATA%\DesktopSticker`；`wallpaperRoot` = 若壁纸模块可用则取 `wpModule_->GetSettings().libraryRoot`，否则空
  > 需要 `#include "desktopsticker/IWallPaperModule.h"`；`Host.h` 已包含它。
- **`Init` 返回 false 时不要丢弃实例**：保留 `rmModule_`，让调用方据 `Available()` 置灰菜单项（规格 3.3）
- 任何失败都收干净资源、记 `AppLog("resmon", ...)`、返回 false，异常不得穿透

- [ ] **Step 3: 在 App 启动路径加载**

在 `App.xaml.cpp` 里 `LoadWallPaper()` 之后调用 `m_host->LoadResMon()`；返回值**只记录日志**，不弹提示、不阻断启动。

同时命名 UI 线程（供 CPU 页显示可读线程名）：

```cpp
SetThreadDescription(GetCurrentThread(), L"UI / 分区渲染");
```

- [ ] **Step 4: 托盘菜单加入口（不可用置灰）**

`MainWindow::OnTrayMessage` 中，在"恢复桌面"之后插入：

```cpp
const bool resmonOk = (m_host && m_host->ResMon() && m_host->ResMon()->Available());
AppendMenuW(menu, MF_STRING | (resmonOk ? 0u : MF_GRAYED), 4, L"资源管理器");
```

并在既有 try/catch 内新增分支（顺序放在 cmd == 3 之前）：

```cpp
} else if (cmd == 4 && resmonOk) {
    m_host->ResMon()->Show();
} else if (cmd == 3) {
```

- [ ] **Step 5: 编译并验证降级路径**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat
```

验证 A：DLL 在 → 托盘右键出现"资源管理器"（本任务下应为**置灰**，因 `Init` 暂返回 false）。
验证 B：把 `DesktopSticker.ResMon.dll` 改名 → 托盘项**仍出现但置灰**，分区/搜索/时钟/壁纸全部正常；改回。

- [ ] **Step 6: 跑测试 + Commit**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && ./Tests/DesktopSticker.Tests.exe   # 87 passed
cd "D:/project/Desktop Sticker"
git commit -m "feat(resmon): load resmon module from host and add tray entry with graceful degradation"
```

---

### Task 3: 线程命名（让 CPU 页可读）

**Files:**
- Modify: `DesktopSticker.Features/src/DirectoryWatcher.cpp:22`
- Modify: `DesktopSticker.Features/src/HotkeyService.cpp:106`
- Modify: `DesktopSticker.Features/src/widgets/WeatherService.cpp:38`
- Modify: `DesktopSticker.WallPaper/src/FrameSchedulerLoop.cpp:36`
- Modify: `DesktopSticker.WallPaper/src/WallPaperModule.cpp:525`、`:550`

**Interfaces:**
- Consumes: 无（Win10 1607+ 的 `SetThreadDescription`）
- Produces: 具名线程，供 `ProcessSampler::GetThreadDescription` 读取

> 这是**纯附加、零行为改动**：只在每个线程体开头加一行设置调试用的线程描述。

- [ ] **Step 1: 逐个在线程 lambda 开头加命名**

在每处 `std::thread([...]{ ... })` 的 lambda 体首行插入（把字符串换成对应名字）：

```cpp
SetThreadDescription(GetCurrentThread(), L"目录监视");
```

对应关系：

| 文件:行 | 线程名 |
|---|---|
| `Features/src/DirectoryWatcher.cpp:22` | `L"目录监视"` |
| `Features/src/HotkeyService.cpp:106` | `L"热键钩子"` |
| `Features/src/widgets/WeatherService.cpp:38` | `L"天气后台"` |
| `WallPaper/src/FrameSchedulerLoop.cpp:36` | `L"壁纸渲染与帧调度"` |
| `WallPaper/src/WallPaperModule.cpp:525` | `L"壁纸转码工作"` |
| `WallPaper/src/WallPaperModule.cpp:550` | `L"壁纸暂停监控"` |

（UI 线程已在 Task 2 Step 3 命名为 `L"UI / 分区渲染"`。）

- [ ] **Step 2: 编译并人工确认命名生效**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat
```
启动应用后用 PowerShell 读回线程描述验证（`GetThreadDescription` 需 P/Invoke，或用 Task 8 之后的资源管理器页面确认）：

```bash
powershell -NoProfile -Command "
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public class TD {
  [DllImport(\"kernel32.dll\")] public static extern IntPtr OpenThread(uint a, bool b, uint tid);
  [DllImport(\"kernel32.dll\")] public static extern int GetThreadDescription(IntPtr h, out IntPtr p);
  [DllImport(\"kernel32.dll\")] public static extern bool CloseHandle(IntPtr h);
  [DllImport(\"kernel32.dll\")] public static extern bool SetThreadDescription(IntPtr h, string d);
}
'@
Write-Host 'compiled'
"
```
（本步只为确认 P/Invoke 可用；真正的端到端确认在 Task 15。）

- [ ] **Step 3: 跑测试 + 人工确认分区/时钟/壁纸无回归 + Commit**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && ./Tests/DesktopSticker.Tests.exe   # 87 passed
cd "D:/project/Desktop Sticker"
git commit -m "feat: name app threads for readable cpu attribution"
```

---

## Phase B — 纯函数（header-only，TDD）

> 本阶段全部 header-only 纯函数。Task 4 负责把 include 目录接进测试工程，后续直接复用。

### Task 4: 测试工程接入 + Format 格式化与百分比

**Files:**
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`
- Create: `Desktop Sticker/DesktopSticker.ResMon/include/desktopsticker/resmon/Format.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestResMonFormat.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `std::wstring format_bytes(uint64_t)` —— 1024 进制，B 整数，KB+ 保留 1 位并去掉末尾 `.0`
  - `std::wstring format_percent(double)` —— 1 位小数 + `%`
  - `std::vector<double> compute_percentages(const std::vector<uint64_t>&)` —— 同序、合计恰为 100.0（最大类补差），总计 0 时全 0

- [ ] **Step 1: 测试工程加 include 目录**

在 `DesktopSticker.Tests.vcxproj` 两处 `AdditionalIncludeDirectories` 追加：
`$(ProjectDir)..\DesktopSticker.ResMon\include;`

- [ ] **Step 2: 写失败测试 `TestResMonFormat.cpp`**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/Format.h>

#include <numeric>

using namespace desktopsticker::resmon;

TEST(Format_BytesIntegerBelowKb) {
    ASSERT_STREQ(L"0 B", format_bytes(0));
    ASSERT_STREQ(L"1023 B", format_bytes(1023));
}

TEST(Format_BytesStripsTrailingZero) {
    ASSERT_STREQ(L"1 KB", format_bytes(1024));
    ASSERT_STREQ(L"4 KB", format_bytes(4096));
}

TEST(Format_BytesKeepsOneDecimal) {
    ASSERT_STREQ(L"54.3 GB", format_bytes(58300000000ull));
    ASSERT_STREQ(L"1.5 MB", format_bytes(1572864ull));
}

TEST(Format_BytesTeraScale) {
    ASSERT_STREQ(L"1 TB", format_bytes(1024ull * 1024 * 1024 * 1024));
}

TEST(Format_PercentOneDecimal) {
    ASSERT_STREQ(L"54.3%", format_percent(54.28));
    ASSERT_STREQ(L"0%", format_percent(0.0));
    ASSERT_STREQ(L"100%", format_percent(100.0));
}

TEST(Percentages_ZeroTotal_AllZero) {
    const auto p = compute_percentages({0, 0, 0});
    ASSERT_EQ(static_cast<size_t>(3), p.size());
    ASSERT_TRUE(p[0] == 0.0 && p[1] == 0.0 && p[2] == 0.0);
}

TEST(Percentages_SingleCategoryIsHundred) {
    const auto p = compute_percentages({12345});
    ASSERT_EQ(1u, static_cast<unsigned>(p.size()));
    ASSERT_TRUE(p[0] == 100.0);
}

TEST(Percentages_SumIsExactlyHundred) {
    // 三个质数份额，四舍五入后必然有偏差，必须由最大类补差修平
    const auto p = compute_percentages({333333, 333333, 333334});
    const double sum = std::accumulate(p.begin(), p.end(), 0.0);
    ASSERT_TRUE(sum > 99.999 && sum < 100.001);
}

TEST(Percentages_EqualSharesTieBreakIsDeterministic) {
    // 三等分必然产生舍入余量，补差给下标最小的最大类，结果必须确定
    const auto p = compute_percentages({1, 1, 1});
    const double sum = std::accumulate(p.begin(), p.end(), 0.0);
    ASSERT_TRUE(sum > 99.999 && sum < 100.001);
    ASSERT_TRUE(p[0] >= p[1] && p[0] >= p[2]);
    ASSERT_TRUE(p[1] == p[2]);
}
```

- [ ] **Step 3: 把新测试文件加进 vcxproj 并运行，确认失败**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: 编译失败（找不到 `Format.h`）。

- [ ] **Step 4: 写实现 `Format.h`**

```cpp
#pragma once

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace desktopsticker::resmon {

namespace detail {
inline std::wstring strip_trailing_zero(std::wstring s) {
    if (s.find(L'.') == std::wstring::npos) return s;
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}
} // namespace detail

// 1024 进制；B 为整数，KB 及以上保留 1 位小数并去掉末尾的 .0
// （与参考形态的 "4 KB" / "54.3 GB" 一致）
inline std::wstring format_bytes(uint64_t bytes) {
    static const wchar_t* kUnits[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    if (bytes < 1024ull) return std::to_wstring(bytes) + L" B";

    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.1f", value);
    return detail::strip_trailing_zero(std::wstring(buf)) + L" " + kUnits[unit];
}

inline std::wstring format_percent(double percent) {
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.1f", percent);
    return detail::strip_trailing_zero(std::wstring(buf)) + L"%";
}

// 同序百分比：保留 1 位小数，四舍五入的余量由最大类补差吸收，
// 保证合计恰为 100.0（总计为 0 时全部返回 0.0）。
// 并列最大时取下标最小者，结果确定。
inline std::vector<double> compute_percentages(const std::vector<uint64_t>& bytes) {
    std::vector<double> out(bytes.size(), 0.0);
    if (bytes.empty()) return out;

    uint64_t total = 0;
    for (uint64_t b : bytes) total += b;
    if (total == 0) return out;

    std::vector<long long> tenths(bytes.size(), 0);
    long long sum = 0;
    size_t largest = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        const double raw = static_cast<double>(bytes[i]) * 1000.0 / static_cast<double>(total);
        tenths[i] = static_cast<long long>(raw + 0.5);   // 0.1% 为单位
        sum += tenths[i];
        if (bytes[i] > bytes[largest]) largest = i;
    }
    tenths[largest] += (1000 - sum);                     // 最大类补差
    if (tenths[largest] < 0) tenths[largest] = 0;

    for (size_t i = 0; i < bytes.size(); ++i) out[i] = static_cast<double>(tenths[i]) / 10.0;
    return out;
}

} // namespace desktopsticker::resmon
```

- [ ] **Step 5: 运行测试，确认 87 + 9 = 96 passed**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: `96 passed, 0 failed`

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(resmon): byte/percent formatting and percentage apportionment with unit tests"
```

---

### Task 5: Classify 存储分类（9 类有序规则）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/include/desktopsticker/resmon/Classify.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestResMonClassify.cpp`
- Modify: `DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: 无
- Produces:
  - `enum class StorageCategory { Wallpaper, Ffmpeg, Assets, Pdb, Program, Runtime, Config, Logs, Other, Count }`
  - `struct ClassifyRoots { std::wstring exeDir, configDir, wallpaperRoot; }`
  - `StorageCategory classify_path(const std::wstring& path, const ClassifyRoots& roots)`
  - `const wchar_t* category_name(StorageCategory)`、`const wchar_t* category_id(StorageCategory)`、`uint32_t category_color(StorageCategory)`（0xRRGGBB）
  - `const wchar_t* category_note(StorageCategory)`（`Pdb` → "可安全删除（仅影响调试）"，`Runtime` → "不建议删除"；其余返回 `nullptr`）

- [ ] **Step 1: 写失败测试 `TestResMonClassify.cpp`**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/Classify.h>

using namespace desktopsticker::resmon;

static ClassifyRoots roots() {
    ClassifyRoots r;
    r.exeDir = L"C:\\App";
    r.configDir = L"C:\\Users\\u\\AppData\\Roaming\\DesktopSticker";
    r.wallpaperRoot = L"E:\\DesktopSticker\\Wallpaper";
    return r;
}

TEST(Classify_WallpaperRootAnywhereOnAnyDrive) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\media\\a\\source.mp4", r) == StorageCategory::Wallpaper);
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\library.json", r) == StorageCategory::Wallpaper);
}

TEST(Classify_FfmpegPayload) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\ffmpeg\\ffmpeg.exe", r) == StorageCategory::Ffmpeg);
}

TEST(Classify_FfmpegRuleIsComponentAware) {
    // 前缀相似但不同的目录绝不能命中 ffmpeg 规则
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\ffmpeg-sdk\\include\\x.h", r) == StorageCategory::Runtime);
}

TEST(Classify_AssetsAndPdbAndProgram) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\assets\\weather\\S2\\100.png", r) == StorageCategory::Assets);
    ASSERT_TRUE(classify_path(L"C:\\App\\Desktop_Sticker.pdb", r) == StorageCategory::Pdb);
    ASSERT_TRUE(classify_path(L"C:\\App\\Desktop_Sticker.exe", r) == StorageCategory::Program);
    ASSERT_TRUE(classify_path(L"C:\\App\\DesktopSticker.Features.dll", r) == StorageCategory::Program);
    ASSERT_TRUE(classify_path(L"C:\\App\\DesktopSticker.ResMon.dll", r) == StorageCategory::Program);
}

TEST(Classify_PdbMustBeDirectChildOfExeDir) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\sub\\other.pdb", r) == StorageCategory::Runtime);
}

TEST(Classify_RuntimeIsExeDirCatchAll) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"C:\\App\\Microsoft.ui.xaml.dll", r) == StorageCategory::Runtime);
    ASSERT_TRUE(classify_path(L"C:\\App\\resmon\\index.html", r) == StorageCategory::Runtime);
}

TEST(Classify_ConfigAndLogs) {
    const auto r = roots();
    const std::wstring cfg = r.configDir;
    ASSERT_TRUE(classify_path(cfg + L"\\config.json", r) == StorageCategory::Config);
    ASSERT_TRUE(classify_path(cfg + L"\\wallpaper.json", r) == StorageCategory::Config);
    ASSERT_TRUE(classify_path(cfg + L"\\debug.log", r) == StorageCategory::Logs);
    ASSERT_TRUE(classify_path(cfg + L"\\debug.log.bak", r) == StorageCategory::Logs);
    ASSERT_TRUE(classify_path(cfg + L"\\something.txt", r) == StorageCategory::Other);
}

TEST(Classify_IsCaseInsensitive) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"c:\\app\\FFMPEG\\ffmpeg.exe", r) == StorageCategory::Ffmpeg);
}

TEST(Classify_EmptyWallpaperRootSkipsRule) {
    ClassifyRoots r = roots();
    r.wallpaperRoot.clear();
    ASSERT_TRUE(classify_path(L"E:\\DesktopSticker\\Wallpaper\\library.json", r) == StorageCategory::Other);
}

TEST(Classify_OutsideAllRootsIsOther) {
    const auto r = roots();
    ASSERT_TRUE(classify_path(L"D:\\somewhere\\else\\x.bin", r) == StorageCategory::Other);
}

TEST(Classify_EveryCategoryHasNameAndId) {
    for (int i = 0; i < static_cast<int>(StorageCategory::Count); ++i) {
        const auto c = static_cast<StorageCategory>(i);
        ASSERT_TRUE(category_name(c) != nullptr && *category_name(c) != L'\0');
        ASSERT_TRUE(category_id(c) != nullptr && *category_id(c) != L'\0');
    }
}
```

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

Expected: 编译失败（找不到 `Classify.h`）

- [ ] **Step 3: 写实现 `Classify.h`**

要点：
- 内部 `normalize`：全小写 + 反斜杠统一
- `is_under(path, root)`：把 root 末尾补 `\\` 后做前缀比较；`root` 为空返回 false；`path == root` 返回 false（目录自身不归入其内容分类）
- `is_direct_child(path, root)`：`path` 的父目录等于 `root`
- `file_name(path)`、`extension(path)`
- 顺序即规格 6.3 的 9 条规则，首个命中即返回

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace desktopsticker::resmon {

enum class StorageCategory {
    Wallpaper, Ffmpeg, Assets, Pdb, Program, Runtime, Config, Logs, Other, Count
};

struct ClassifyRoots {
    std::wstring exeDir;
    std::wstring configDir;
    std::wstring wallpaperRoot;   // 可空
};

namespace detail {

inline std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

inline std::wstring trim_sep(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

inline std::wstring parent_of(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring{} : p.substr(0, pos);
}

inline std::wstring name_of(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

inline std::wstring ext_of(const std::wstring& p) {
    const std::wstring n = name_of(p);
    const size_t pos = n.find_last_of(L'.');
    return pos == std::wstring::npos ? std::wstring{} : n.substr(pos);
}

// 路径分量感知：C:\a\ffmpeg-sdk 不属于 C:\a\ffmpeg
inline bool is_under(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    const std::wstring p = lower(trim_sep(path));
    const std::wstring r = lower(trim_sep(root));
    if (p.size() <= r.size()) return false;
    if (p.compare(0, r.size(), r) != 0) return false;
    return p[r.size()] == L'\\' || p[r.size()] == L'/';
}

inline bool is_direct_child(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    return lower(trim_sep(parent_of(path))) == lower(trim_sep(root));
}

} // namespace detail

inline StorageCategory classify_path(const std::wstring& path, const ClassifyRoots& roots) {
    using namespace detail;

    // 1 壁纸媒体库
    if (is_under(path, roots.wallpaperRoot)) return StorageCategory::Wallpaper;
    // 2 FFmpeg 负载
    if (is_under(path, roots.exeDir + L"\\ffmpeg")) return StorageCategory::Ffmpeg;
    // 3 天气图标资源
    if (is_under(path, roots.exeDir + L"\\assets")) return StorageCategory::Assets;
    // 4 调试符号（exeDir 直接子文件）
    if (is_direct_child(path, roots.exeDir) && lower(ext_of(path)) == L".pdb") {
        return StorageCategory::Pdb;
    }
    // 5 程序主体
    if (is_direct_child(path, roots.exeDir)) {
        const std::wstring n = lower(name_of(path));
        if (n == L"desktop_sticker.exe" || n == L"desktopsticker.features.dll" ||
            n == L"desktopsticker.wallpaper.dll" || n == L"desktopsticker.resmon.dll") {
            return StorageCategory::Program;
        }
    }
    // 6 运行时与框架（exeDir 兜底）
    if (is_under(path, roots.exeDir)) return StorageCategory::Runtime;
    // 7 配置与布局
    if (is_direct_child(path, roots.configDir)) {
        const std::wstring n = lower(name_of(path));
        if (n == L"config.json" || n == L"apps.json" ||
            n == L"layout.json" || n == L"wallpaper.json") {
            return StorageCategory::Config;
        }
        // 8 日志
        if (n == L"debug.log" || lower(ext_of(path)) == L".bak") {
            return StorageCategory::Logs;
        }
    }
    // 9 其他
    return StorageCategory::Other;
}

inline const wchar_t* category_id(StorageCategory c) {
    switch (c) {
        case StorageCategory::Wallpaper: return L"wallpaper";
        case StorageCategory::Ffmpeg:    return L"ffmpeg";
        case StorageCategory::Assets:    return L"assets";
        case StorageCategory::Pdb:       return L"pdb";
        case StorageCategory::Program:   return L"program";
        case StorageCategory::Runtime:   return L"runtime";
        case StorageCategory::Config:    return L"config";
        case StorageCategory::Logs:      return L"logs";
        default:                         return L"other";
    }
}

inline const wchar_t* category_name(StorageCategory c) {
    switch (c) {
        case StorageCategory::Wallpaper: return L"壁纸媒体库";
        case StorageCategory::Ffmpeg:    return L"FFmpeg 负载";
        case StorageCategory::Assets:    return L"天气图标资源";
        case StorageCategory::Pdb:       return L"调试符号";
        case StorageCategory::Program:   return L"程序主体";
        case StorageCategory::Runtime:   return L"运行时与框架";
        case StorageCategory::Config:    return L"配置与布局";
        case StorageCategory::Logs:      return L"日志";
        default:                         return L"其他";
    }
}

inline const wchar_t* category_note(StorageCategory c) {
    switch (c) {
        case StorageCategory::Pdb:     return L"可安全删除（仅影响调试）";
        case StorageCategory::Runtime: return L"不建议删除";
        default:                       return nullptr;
    }
}

inline uint32_t category_color(StorageCategory c) {
    switch (c) {
        case StorageCategory::Wallpaper: return 0x2E7DD1;
        case StorageCategory::Ffmpeg:    return 0x6CA642;
        case StorageCategory::Assets:    return 0x9B6BD1;
        case StorageCategory::Pdb:       return 0xD15B5B;
        case StorageCategory::Program:   return 0xD19B3B;
        case StorageCategory::Runtime:   return 0x4FB0A5;
        case StorageCategory::Config:    return 0x8A8FA3;
        case StorageCategory::Logs:      return 0xB08A3B;
        default:                         return 0x6E6E6E;
    }
}

} // namespace desktopsticker::resmon
```

> `towlower` 需要 `<cwctype>`，一并 include。

- [ ] **Step 4: 运行测试，确认 96 + 11 = 107 passed**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(resmon): ordered storage path classification with unit tests"
```

---

### Task 6: CpuMath 采样差值

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/include/desktopsticker/resmon/CpuMath.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestResMonCpuMath.cpp`
- Modify: `DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: 无
- Produces:
  - `struct CpuSample { int64_t kernel100ns; int64_t user100ns; int64_t wall100ns; }`
  - `struct CpuDelta { double percent; bool baseline; }`
  - `CpuDelta cpu_percent(const CpuSample& prev, const CpuSample& now)`

- [ ] **Step 1: 写失败测试**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/CpuMath.h>

using namespace desktopsticker::resmon;

TEST(CpuMath_HalfBusy) {
    CpuSample prev{0, 0, 0};
    CpuSample now{500'000'000ll, 500'000'000ll, 2'000'000'000ll};  // 忙 1s / 墙钟 2s
    const auto d = cpu_percent(prev, now);
    ASSERT_FALSE(d.baseline);
    ASSERT_TRUE(d.percent > 49.9 && d.percent < 50.1);
}

TEST(CpuMath_FirstSampleHasNoBaseline) {
    CpuSample prev{};
    CpuSample now{100'000'000ll, 0, 0};           // 墙钟差为 0
    const auto d = cpu_percent(prev, now);
    ASSERT_TRUE(d.baseline);
    ASSERT_TRUE(d.percent == 0.0);
}

TEST(CpuMath_ZeroWallClockIsBaseline) {
    CpuSample prev{0, 0, 1000};
    CpuSample now{100, 0, 1000};
    ASSERT_TRUE(cpu_percent(prev, now).baseline);
}

TEST(CpuMath_NegativeBusyIsBaseline) {
    // 线程被复用或计数回绕时不得给出负数
    CpuSample prev{1000, 1000, 0};
    CpuSample now{10, 10, 1'000'000'000ll};
    const auto d = cpu_percent(prev, now);
    ASSERT_TRUE(d.baseline);
    ASSERT_TRUE(d.percent == 0.0);
}

TEST(CpuMath_CapsAtHundred) {
    CpuSample prev{0, 0, 0};
    CpuSample now{5'000'000'000ll, 0, 1'000'000'000ll};   // 忙 5s / 墙钟 1s
    const auto d = cpu_percent(prev, now);
    ASSERT_TRUE(d.percent <= 100.0);
}
```

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

- [ ] **Step 3: 写实现**

```cpp
#pragma once

#include <cstdint>

namespace desktopsticker::resmon {

// 时间单位统一为 100ns（GetThreadTimes 的 FILETIME 与 QPC 换算后）
struct CpuSample {
    int64_t kernel100ns = 0;
    int64_t user100ns = 0;
    int64_t wall100ns = 0;
};

struct CpuDelta {
    double percent = 0.0;
    bool baseline = false;   // true 表示本次无有效基线（首次采样/墙钟为 0/计数回绕）
};

// 单线程上限 100%（不折算多核总容量）；无法计算时返回 baseline=true、percent=0
inline CpuDelta cpu_percent(const CpuSample& prev, const CpuSample& now) {
    CpuDelta d;
    const int64_t wall = now.wall100ns - prev.wall100ns;
    if (wall <= 0) {
        d.baseline = true;
        return d;
    }
    const int64_t busy = (now.kernel100ns - prev.kernel100ns) +
                         (now.user100ns - prev.user100ns);
    if (busy < 0) {
        d.baseline = true;
        return d;
    }
    d.percent = static_cast<double>(busy) * 100.0 / static_cast<double>(wall);
    if (d.percent > 100.0) d.percent = 100.0;
    return d;
}

} // namespace desktopsticker::resmon
```

- [ ] **Step 4: 运行测试，确认 107 + 5 = 112 passed**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(resmon): cpu sample delta math with unit tests"
```

---

### Task 7: JsonBuild 快照与响应组装

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/include/desktopsticker/resmon/JsonBuild.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestResMonJson.cpp`
- Modify: `DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: `Format.h`、`Classify.h`
- Produces:
  - `std::string to_utf8(const std::wstring&)` / `std::wstring from_utf8(const std::string&)`
  - 快照结构体：
    ```cpp
    struct ThreadRow  { uint32_t tid; std::wstring name; double cpuPercent; int64_t totalMs; bool baseline; };
    struct ChildRow   { uint32_t pid; std::wstring name; double cpuPercent; uint64_t workingSetBytes; };
    struct CpuSnapshot    { double totalPercent; std::vector<ThreadRow> threads; std::vector<ChildRow> children; bool baseline; };
    struct ModuleRow  { std::wstring name; uint64_t imageBytes; };
    struct MemorySnapshot { uint64_t workingSetBytes, privateBytes, peakWorkingSetBytes;
                            std::vector<ModuleRow> modules; std::vector<ChildRow> children; };
    struct StorageRow { StorageCategory category; uint64_t bytes; double percent; };
    struct StorageSnapshot { uint64_t totalBytes; std::vector<StorageRow> rows;
                             std::vector<std::wstring> warnings; int64_t scannedAtMs; };
    ```
  - `std::string build_cpu_response(const CpuSnapshot&, int64_t sampledAtMs)`
  - `std::string build_memory_response(const MemorySnapshot&)`
  - `std::string build_storage_response(const StorageSnapshot&)`
  - `std::string build_error_response(const std::wstring& cmd, const std::wstring& message)`

> **实现取舍（对规格 5.2 的落实方式）**：JSON 组装用项目已有的 **nlohmann/json**（include 根 `third_party`），转义由库保证，比自己手写转义更安全；可测性由"纯函数式响应组装器"承担 —— 组装器只吃纯数据结构，不碰任何系统调用。规格里"反斜杠/中文/引号/控制字符"的转义要求由 `to_utf8` 往返 + nlohmann dump/parse 往返测试覆盖。

- [ ] **Step 1: 写失败测试 `TestResMonJson.cpp`**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/resmon/JsonBuild.h>

#include <nlohmann/json.hpp>

using namespace desktopsticker::resmon;
using nlohmann::json;

TEST(JsonBuild_Utf8RoundTripChinese) {
    ASSERT_STREQ(L"壁纸媒体库", from_utf8(to_utf8(L"壁纸媒体库")));
}

TEST(JsonBuild_Utf8RoundTripPathWithBackslashes) {
    const std::wstring p = L"E:\\DesktopSticker\\Wallpaper\\media\\测试\\source.mp4";
    ASSERT_STREQ(p.c_str(), from_utf8(to_utf8(p)).c_str());
}

TEST(JsonBuild_StorageResponseRoundTripsThroughJsonParser) {
    StorageSnapshot s;
    s.totalBytes = 1024;
    s.scannedAtMs = 1234567890;
    s.rows.push_back({StorageCategory::Wallpaper, 1000, 97.7});
    s.rows.push_back({StorageCategory::Pdb, 24, 2.3});
    s.warnings.push_back(L"路径不存在: E:\\不存在的目录");

    const std::string dumped = build_storage_response(s);
    const json j = json::parse(dumped);      // 必须能被解析

    ASSERT_EQ(std::string("storage"), j.at("type").get<std::string>());
    ASSERT_EQ(1024ull, j.at("totalBytes").get<uint64_t>());
    ASSERT_EQ(2u, static_cast<unsigned>(j.at("categories").size()));
    // 反斜杠与中文经 JSON 往返不丢
    ASSERT_TRUE(from_utf8(j.at("warnings").at(0).get<std::string>()) ==
                L"路径不存在: E:\\不存在的目录");
    ASSERT_TRUE(from_utf8(j.at("categories").at(0).at("name").get<std::string>()) ==
                L"壁纸媒体库");
}

TEST(JsonBuild_CpuResponseShape) {
    CpuSnapshot s;
    s.baseline = false;
    s.totalPercent = 12.5;
    s.threads.push_back({1234, L"壁纸渲染与帧调度", 8.0, 800, false});
    s.children.push_back({4321, L"ffmpeg.exe", 40.0, 1024 * 1024});

    const json j = json::parse(build_cpu_response(s, 999));
    ASSERT_EQ(std::string("cpu"), j.at("type").get<std::string>());
    ASSERT_EQ(999, j.at("sampledAtMs").get<int64_t>());
    ASSERT_EQ(1u, static_cast<unsigned>(j.at("threads").size()));
    ASSERT_TRUE(from_utf8(j.at("threads").at(0).at("name").get<std::string>()) == L"壁纸渲染与帧调度");
    ASSERT_EQ(1u, static_cast<unsigned>(j.at("children").size()));
}

TEST(JsonBuild_MemoryResponseShape) {
    MemorySnapshot s;
    s.workingSetBytes = 200ull * 1024 * 1024;
    s.privateBytes = 150ull * 1024 * 1024;
    s.peakWorkingSetBytes = 300ull * 1024 * 1024;
    s.modules.push_back({L"DesktopSticker.ResMon.dll", 4096});

    const json j = json::parse(build_memory_response(s));
    ASSERT_EQ(std::string("memory"), j.at("type").get<std::string>());
    ASSERT_EQ(200ull * 1024 * 1024, j.at("workingSetBytes").get<uint64_t>());
    ASSERT_EQ(1u, static_cast<unsigned>(j.at("modules").size()));
}

TEST(JsonBuild_ErrorResponse) {
    const json j = json::parse(build_error_response(L"storage", L"扫描失败"));
    ASSERT_EQ(std::string("error"), j.at("type").get<std::string>());
    ASSERT_TRUE(from_utf8(j.at("message").get<std::string>()) == L"扫描失败");
}
```

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

- [ ] **Step 3: 写实现 `JsonBuild.h`**

要点：
- `to_utf8` / `from_utf8` 用 `WideCharToMultiByte`/`MultiByteToWideChar`（CP_UTF8，失败返回空串）
- 每个 `build_*_response` 用 `nlohmann::json` 组装后 `dump()`；字段名与规格 5.1 的契约逐一对应
- `StorageRow` 输出 `id` / `name` / `bytes` / `percent` / `color`（`"#RRGGBB"` 字符串，由 `category_color` 格式化）/ `note`（无则省略）
- `ThreadRow.name` 为空时输出 `"线程 <tid>"`，避免前端处理空值
- `type` 字段固定为 `"cpu"` / `"memory"` / `"storage"` / `"error"`

- [ ] **Step 4: 运行测试，确认 112 + 6 = 118 passed**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(resmon): snapshot structs and json response builders with unit tests"
```

---

## Phase C — 采集器（薄封装系统调用）

### Task 8: ProcessSampler（按线程 CPU + 子进程）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/src/ProcessSampler.h`、`.cpp`
- Modify: `DesktopSticker.ResMon.vcxproj`

**Interfaces:**
- Consumes: `CpuMath.h`、`JsonBuild.h`
- Produces:
  - `class ProcessSampler { CpuSnapshot Sample(); private: std::map<uint32_t, CpuSample> prev_; }`
  - `int64_t qpc_now_100ns()`（QPC → 100ns，供全项目复用）

- [ ] **Step 1: 实现 `qpc_now_100ns()`**

`QueryPerformanceFrequency` 只取一次（`static`），`QueryPerformanceCounter` 换算到 100ns。频率为 0 时返回 0（调用方按 baseline 处理）。

- [ ] **Step 2: 实现线程采样**

- `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)`，`Thread32First/Next` 过滤 `th32OwnerProcessID == GetCurrentProcessId()`
- 每线程 `OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid)`；失败（线程已退出）跳过
- `GetThreadTimes` 取 `KernelTime`/`UserTime` 的 FILETIME → 100ns（`(hi<<32)|lo`）
- `GetThreadDescription` 取名字（失败则空）
- 与 `prev_` 中同 tid 的样本做 `cpu_percent`；**本次样本存入 `prev_` 供下次使用**；无基线时 `baseline=true`
- 线程消失时从 `prev_` 移除，避免 `prev_` 无限增长
- 总百分比 = 各线程百分比之和，上限 100.0（单进程视角）
- 按 `cpuPercent` 降序，再按 tid 升序（稳定顺序）

- [ ] **Step 3: 实现子进程采样**

- `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)`，过滤 `th32ParentProcessID == GetCurrentProcessId()`
- 每个子进程：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` → `GetProcessTimes`（用同一套 `prev_`，键为 pid，与线程键区分开）+ `GetProcessMemoryInfo`
- 名字取 `PROCESSENTRY32.szExeFile`

- [ ] **Step 4: 接入 `WebViewHost` 之外的自检**

本任务尚无 UI，用一个临时的验证手段确认数据合理：在 `ResMonModule::Init` 中调用一次 `ProcessSampler::Sample()` 并把线程数写进 `debug.log`（`[resmon] threads=N children=M`）。Task 13 会移除这段临时日志。

- [ ] **Step 5: 编译 + 人工确认日志**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat
```
启动应用后 `grep '\[resmon\]' %APPDATA%\DesktopSticker\debug.log`，期望 `threads=` 为个位数且 `children=0`（未转码时）。

- [ ] **Step 6: 跑测试（应仍为 118）+ Commit**

```bash
git commit -m "feat(resmon): per-thread cpu and child-process sampler"
```

---

### Task 9: MemorySampler（进程内存 + 模块）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/src/MemorySampler.h`、`.cpp`
- Modify: `DesktopSticker.ResMon.vcxproj`

**Interfaces:**
- Consumes: `JsonBuild.h`
- Produces: `class MemorySampler { MemorySnapshot Sample(); }`

- [ ] **Step 1: 实现**

- `GetProcessMemoryInfo` 配 `PROCESS_MEMORY_COUNTERS_EX` 取 `WorkingSetSize`、`PrivateUsage`、`PeakWorkingSetSize`
- `EnumProcessModules`（先取所需缓冲区大小再分配）→ 逐个 `GetModuleInformation` 取 `SizeOfImage`；模块名 `GetModuleBaseNameW`
- 按 `imageBytes` 降序
- 子进程内存复用 Task 8 的进程枚举（抽成 `ProcessSampler` 的 `SampleChildren()` 供两者调用，避免重复代码）
- **不报虚拟大小**（规格 6.2：PSAPI 不提供，不为一个展示项引入未文档化接口）

- [ ] **Step 2: 编译并确认模块数合理**

同 Task 8 的方式临时记录 `[resmon] modules=N`（本机自包含部署，模块数应为数十级别）。Task 13 移除。

- [ ] **Step 3: 跑测试 + Commit**

```bash
git commit -m "feat(resmon): process memory and loaded-module sampler"
```

---

### Task 10: StorageScanner(9 类扫描，工作线程)

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/src/StorageScanner.h`、`.cpp`
- Modify: `DesktopSticker.ResMon.vcxproj`

**Interfaces:**
- Consumes: `Classify.h`、`Format.h`、`JsonBuild.h`
- Produces:
  - `class StorageScanner { StorageSnapshot Scan(const ResMonPaths&, std::mutex& logMutex); }`
  - 扫描是同步的；由调用方放到工作线程（Task 13）

- [ ] **Step 1: 实现扫描**

要求：
- 三个根：`paths.exeDir`、`paths.configDir`、`paths.wallpaperRoot`（空则跳过并把原因写入 `warnings`）
- 用 `std::filesystem::recursive_directory_iterator` 配 `std::error_code` 重载（**不用抛异常版本**），逐文件 `file_size`，`classify_path` 归类累加
- 遇到错误（无权限/被占用/目录中途消失）→ `it.increment(ec)` 跳过并在 `warnings` 里累积**去重后**的简短描述，避免上千条重复警告
- 三个根可能有包含关系（`configDir` 一般不在 `exeDir` 下，但需防御）：若 `is_under(configDir, exeDir)` 则 configDir 跳过，避免重复计数
- `totalBytes` = 9 类之和；`percent` 用 `compute_percentages` 一次算出（传入 9 类字节数数组，保持枚举顺序）
- 只读：只做 `file_size` / 目录遍历，不创建/修改/删除
- `scannedAtMs` = `GetSystemTimeAsFileTime` 换算的 Unix 毫秒

- [ ] **Step 2: 编译 + 人工核对数字**

临时在 `Init` 调用一次 Scan 并记录 `[resmon] storage total=<bytes>`。核对：应接近 **352MB + 158MB + 72MB + 3.2MB + 0.3MB ≈ 585MB**（实测基线，见规格第 2 节的实测表）。Task 13 移除临时日志。

- [ ] **Step 3: 跑测试 + Commit**

```bash
git commit -m "feat(resmon): read-only storage scanner with ordered classification"
```

---

## Phase D — WebView2 宿主与桥

### Task 11: ResMonWindow（纯 Win32 顶层窗口）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/src/ResMonWindow.h`、`.cpp`
- Modify: `DesktopSticker.ResMon.vcxproj`

**Interfaces:**
- Consumes: 无
- Produces: `class ResMonWindow { bool Create(); void Destroy(); HWND Handle(); bool Exists(); void Focus(); static bool IsResMonWindow(HWND); }`

- [ ] **Step 1: 实现**

- 窗口类 `DesktopSticker.ResMon.Window`，`WS_OVERLAPPEDWINDOW`，标题 `资源管理器 — Desktop Sticker`
- 初始尺寸 920×640 按 `GetDpiForSystem()` 换算物理像素；用 `AdjustWindowRectExForDpi` 保证客户区尺寸正确
- `WM_SIZE` → 记录客户区尺寸并回调（WebView2 控制器据此 `put_Bounds`）
- `WM_DPICHANGED` → 使用建议矩形并重算 bounds
- `WM_CLOSE` → `DestroyWindow`（销毁而非隐藏；与 WebView2 控制器生命周期绑定，由 Task 12 负责在 `WM_DESTROY` 前释放控制器）
- 单实例：静态 `HWND`；已存在则 `Focus()` 前置（`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`）而不是重复创建
- 图标：复用 EXE 的图标资源（`LoadIconW(GetModuleHandleW(nullptr), ...)`）；取不到则跳过，不失败
- `IsResMonWindow` 供"双击桌面空白"检测等既有逻辑排除自身（与壁纸窗口同样的处理）

- [ ] **Step 2: 编译 + 人工验证窗口本身**

临时在 `ResMonModule::Show()` 里创建窗口并显示纯色背景（尚无 WebView2）。确认：尺寸/DPI 正确、可缩放、关闭后不残留、再次 `Show()` 能重建。Task 12 接上 WebView2。

- [ ] **Step 3: 跑测试 + Commit**

```bash
git commit -m "feat(resmon): native win32 host window"
```

---

### Task 12: WebViewHost（环境、控制器、虚拟主机映射、消息桥）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.ResMon/src/WebViewHost.h`、`.cpp`
- Modify: `DesktopSticker.ResMon.vcxproj`

**Interfaces:**
- Consumes: `Task 11` 的窗口；`ProcessSampler`/`MemorySampler`/`StorageScanner`；`JsonBuild.h`
- Produces:
  - `class WebViewHost { bool Create(HWND, const std::wstring& assetsDir); void Destroy(); void Resize(int w, int h); bool Ready(); }`
  - 命令回调 `void SetCommandHandler(std::function<std::string(const std::wstring& cmd)>)`
- 命令分发约定：`handle_command(cmd)` 返回**响应 JSON 字符串**，空串表示未知命令（此时回 `error`）

- [ ] **Step 1: 创建环境与控制器**

```
CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataDir, nullptr, handler)
  userDataDir = %LOCALAPPDATA%\DesktopSticker\ResMonWebView   ← 不写程序目录
CreateCoreWebView2Controller(hwnd, handler)
  → get_CoreWebView2 → put_IsWebMessageEnabled(TRUE)
  → SetVirtualHostNameToFolderMapping(L"resmon.local", assetsDir, ALLOW)
  → add_WebMessageReceived
  → Navigate(L"https://resmon.local/index.html")
```
用 `Microsoft::WRL::Callback<>` 实现各 handler（异步完成，用事件或 `std::promise` 等首次导航完成）。

失败处理：
- 环境创建失败（WebView2 运行时缺失）→ `Create` 返回 false，`debug.log` 记录 HRESULT
- 控制器创建失败 → 同样返回 false
- 资源目录不存在 → 仍导航（会 404），并由 `NavigationCompleted` 的 `IsSuccess` 为 false 时注入内置错误页（`NavigateToString`）

- [ ] **Step 2: 实现消息桥**

`add_WebMessageReceived` 回调里：
- `get_WebMessageAsJson` → `nlohmann::json::parse` 取 `cmd`
- 未知/解析失败 → 回 `build_error_response`
- 已知 → 调用 `commandHandler_(cmd)`，把返回的 JSON 字符串 `PostWebMessageAsJson`
- **存储命令放到工作线程**执行：在 `ResMonModule`（Task 13）里维护一个 worker，避免阻塞 UI 线程；`WebViewHost` 只负责把结果发回，因此 `commandHandler_` 需要能异步回传 —— 采用：`commandHandler_` 返回空串表示"异步处理中"，由模块在 worker 完成后调用 `WebViewHost::PostResponse(json)`

- [ ] **Step 3: 生命周期**

- `Destroy()`：先 `Close()` 控制器（等 `add_ControllerDestroyed` 或直接 `Release`），再释放 environment，最后交回窗口销毁
- 窗口 `WM_DESTROY` 前必须已释放控制器，避免悬挂

- [ ] **Step 4: 编译 + 人工验证**

启动应用 → 托盘"资源管理器"（此时仍置灰，因 Task 13 未接 `Available`）→ 临时把 `Init` 改为返回 true 验证页面能加载出 `resmon.local`（先放一个占位 `index.html`）。确认无白屏、可缩放。

- [ ] **Step 5: 跑测试 + Commit**

```bash
git commit -m "feat(resmon): webview2 host with virtual host mapping and message bridge"
```

---

### Task 13: ResMonModule 组装、命令分发与异步存储扫描

**Files:**
- Modify: `Desktop Sticker/DesktopSticker.ResMon/src/ResMonModule.cpp`
- Modify: `DesktopSticker.ResMon.vcxproj`（若新增内部头）

**Interfaces:**
- Consumes: Task 8–12 全部
- Produces: 可用的 `IResMonModule` 实现

- [ ] **Step 1: 实现 `Init`**

- 存 `ResMonPaths`
- `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`（WebView2 需要 STA；模块内自持引用计数，`Shutdown` 时 `CoUninitialize`）
- 创建 `WebViewHost` **不在此处**（需要窗口）；`Init` 只创建环境 —— 为简化，`Init` 里创建窗口与 WebView2（窗口不可见），`Show` 只做显示与前置。若创建失败 → `available_ = false`，返回 false
- 移除 Task 8/9/10 的临时日志

- [ ] **Step 2: 实现命令分发**

| cmd | 处理 |
|---|---|
| `cpu` | `ProcessSampler::Sample()` → `build_cpu_response` （同步，毫秒级） |
| `memory` | `MemorySampler::Sample()` → `build_memory_response`（同步） |
| `storage` | 投递到 worker 线程，**立即返回空串**（异步）；完成后 `PostResponse` |
| 其他 | 返回 `build_error_response(cmd, L"未知命令")` |

- `storage` 重入：worker 只保留最新一次请求（丢弃在途旧请求的结果），或串行排队；采用**丢弃旧结果**（用时序号），符合规格 5.3

- [ ] **Step 3: 实现 `Show` / `Shutdown` / `Available`**

- `Show`：窗口不存在则创建；存在则 `Focus()`。返回是否可见
- `Shutdown`：停 worker（join）、销毁 WebView2 控制器与窗口、`CoUninitialize`
- `Available`：`available_`（环境与窗口创建均成功）

- [ ] **Step 4: 编译 + 人工验证**

托盘"资源管理器"不再置灰；打开后 CPU/内存页有数据、每 2s 刷新；点"重新计算"后存储页出现 9 类数据（前端在 Task 14，本步先用浏览器 DevTools 或页面上的占位渲染确认 JSON 正确 —— 用 `--remote-debugging-port` 或直接在页面里 `console.log`）。

- [ ] **Step 5: 跑测试（应仍为 118）+ Commit**

```bash
git commit -m "feat(resmon): module assembly with command dispatch and async storage scan"
```

---

## Phase E — 前端与收尾

### Task 14: 前端资源（原生 HTML/CSS/JS）

**Files:**
- Create: `Desktop Sticker/Desktop Sticker/resmon/index.html`、`style.css`、`app.js`
- Modify: `Desktop Sticker/Desktop Sticker/Desktop Sticker.vcxproj`（PostBuildEvent 复制 `resmon\`）

**Interfaces:**
- Consumes: Task 12 的虚拟主机 `https://resmon.local/` 与 Task 13 的 JSON 契约
- Produces: 三页签界面

- [ ] **Step 1: 写 `index.html`**

结构（与参考形态对齐）：顶部应用名 + 三个页签按钮（CPU / 内存 / 存储）；每个页签一个面板：
- 总量卡片：大号数字 + 副标题
- 占比条：`div` 序列按 `percent` 设 `flex-grow`，颜色取 `color`
- 明细表：`table`，列随页签不同（CPU：线程/TID/CPU%/累计；内存：模块/映像大小；存储：分类/占用/占比/备注）
- 存储页额外：`重新计算` 按钮 + `scannedAt` 时间 + `warnings` 区域

- [ ] **Step 2: 写 `style.css`**

跟随系统深浅色（`prefers-color-scheme`）；Win11 观感：圆角、细边框、`Segoe UI` 字体族；无外部字体/图标依赖（离线可用）。

- [ ] **Step 3: 写 `app.js`**

- `postMessage(JSON.stringify({cmd}))` 发送；`window.chrome.webview.addEventListener('message', …)` 接收
- 每个响应带递增 `seq`；**丢弃 seq 小于已渲染的响应**（防止过期结果覆盖）
- CPU/内存页签激活时 `setInterval(2000)` 拉取；切走或 `document.hidden` 时 `clearInterval`
- 存储页签：进入时若从未扫描过则自动拉一次；`重新计算` 重新拉取，期间按钮禁用并显示"正在计算…"
- 渲染时对字符串做 `textContent` 赋值（**不用 innerHTML**，避免路径中的特殊字符造成注入）
- `warnings` 非空时在页面上以醒目样式列出

- [ ] **Step 4: 构建复制**

在 EXE 的 `PostBuildEvent` 追加：

```
xcopy /Y /E /I "$(ProjectDir)resmon" "$(OutDir)resmon\"
```

- [ ] **Step 5: 编译 + 端到端人工验证**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat
```
逐项确认规格第 9 节的验收清单，重点是：
- CPU 页线程名可读、数值随负载变化
- 转码时出现 `ffmpeg.exe` 子进程项
- 存储页壁纸库 ≈ 352 MB、FFmpeg 负载 ≈ 158 MB，合计等于各分类之和
- 把某分类目录改名 → 出现 warnings 且页面不崩

- [ ] **Step 6: 跑测试 + Commit**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && ./Tests/DesktopSticker.Tests.exe   # 118 passed
cd "D:/project/Desktop Sticker"
git commit -m "feat(resmon): native html/css/js frontend with three tabs"
```

---

### Task 15: 文档与最终验收

**Files:**
- Modify: `README.md`、`AGENTS.md`、`docs/acceptance.md`

**Interfaces:**
- Consumes: 全部前置任务
- Produces: 更新的文档 + 通过验收的功能

- [ ] **Step 1: 跑完整测试基线**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: `118 passed, 0 failed`

- [ ] **Step 2: 逐条走规格第 9 节验收清单（11 条）并记录实际观察**

- [ ] **Step 3: 更新文档**

- `README.md`：功能一览新增"资源管理器"；项目结构加入 `DesktopSticker.ResMon/` 与 `resmon/` 前端资源；使用说明表格加入入口行
- `AGENTS.md`：布局加入新工程；架构边界加入 `IResMonModule` 与"导出宏头文件名必须唯一"的教训；Fragile areas 加入"WebView2 宿主 + 纯 Win32 窗口（刻意不用 WinUI 以避开次级窗口销毁重建）"、"存储分类的 9 类有序规则"、"扫描必须只读且在工作线程"；约定加入"新增 `.ps1` 需 BOM"已有的那条与"ResMon 的 include 需手动加进 vcxproj"
- `docs/acceptance.md`：新增"资源管理器"章节，写入第 9 节清单

- [ ] **Step 4: 最终 Commit**

```bash
git add README.md AGENTS.md docs/acceptance.md
git commit -m "docs: document resmon feature, entry point and acceptance items"
```

---

## 自审记录

**规格覆盖检查**（规格章节 → 任务）：

| 规格章节 | 任务 |
|---|---|
| 3.1 新工程与文件划分 | Task 1 |
| 3.2 EXE ↔ ResMon 边界 | Task 1、2 |
| 3.3 降级要求（Init 失败保留实例） | Task 2 |
| 4.1 纯 Win32 窗口 | Task 11 |
| 4.2 WebView2 环境/虚拟主机映射 | Task 12 |
| 4.3 前端资源与构建复制 | Task 14 |
| 4.4 WebView2Loader.dll 落地 | Task 1（targets 自动复制 + EXE PostBuildEvent 兜底） |
| 5.1 JSON 契约 | Task 7、13 |
| 5.2 为何用 JSON | Task 7 的实现取舍说明 |
| 5.3 刷新与并发（请求号、工作线程） | Task 12、13、14 |
| 6.1 CPU 按线程 + 线程命名 + 子进程 | Task 3、8 |
| 6.2 内存按模块 + 不报虚拟大小 | Task 9 |
| 6.3 存储 9 类有序规则 + warnings + 只读 | Task 5、10 |
| 7.1 待测纯函数（Format/Classify/CpuMath/Json） | Task 4、5、6、7 |
| 7.2 不做单测的部分 | Task 8–12 走人工验证 |
| 8 错误处理与降级 | Task 2、10、12、13 |
| 9 人工验收 | Task 14 Step 5、Task 15 Step 2 |
| 10 未纳入范围 | 无任务（正确，属排除项） |
| 11 风险与对策 | Task 1（loader 落地）、10（工作线程扫描、跨盘分类）、3（线程命名零行为改动） |

**占位符检查**：无 TBD/TODO；每个代码步骤都给了可编译的实义代码或明确的 API 与不变量。

**类型一致性检查**：
- `StorageCategory` 在 Task 5 定义，Task 7（`StorageRow.category`）、Task 10（扫描归类）一致使用。
- `CpuSample` / `CpuDelta`（Task 6）与 Task 8 使用一致。
- `CpuSnapshot` / `MemorySnapshot` / `StorageSnapshot`（Task 7）与 Task 8/9/10 的采集器、Task 13 的分发一致。
- `ThreadRow` / `ChildRow` / `ModuleRow` / `StorageRow`（Task 7）字段与 Task 8/9/10 的填充一致。
- `ResMonPaths`（Task 1）在 Task 2（Host 构造）、Task 13（模块使用）、Task 10（扫描根）一致。
- `IResMonModule` 方法（Task 1）在 Task 2（Host 调用 `LoadResMon`/`ResMon`/`Available`/`Show`）、Task 13（实现）一致。
- `qpc_now_100ns`（Task 8）被 `ProcessSampler` 与 `MemorySampler`（Task 9）共用，命名一致。

**与规格的实现偏离（已在计划内说明）**：
- 规格 5.2/7.1 把"JSON 组装与转义"列为待测纯函数；本计划改用已有的 **nlohmann/json** 组装（转义由库保证），可测性由 Task 7 的"纯函数式响应组装器 + 往返测试"承担。理由：手写转义是纯风险，库已经过验证且是项目既有依赖。
- 规格 4.4 要求 `PostBuildEvent` 显式 xcopy `WebView2Loader.dll`；计划保留该兜底，但同时说明 NuGet targets 已会自动复制到 ResMon 项目 OutDir —— 兜底是为了覆盖"EXE 从自己目录加载 DLL"这条路径（targets 管不到）。
