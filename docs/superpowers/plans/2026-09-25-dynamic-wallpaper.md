# 动态桌面壁纸（DesktopSticker.WallPaper）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Desktop Sticker 增加视频桌面壁纸：MF 为主 + FFmpeg 共享库兜底的解码、D3D11+DXGI+DirectComposition 上屏、嵌入桌面图标之下的分层窗口。

**Architecture:** 新增独立 C++20 Win32 DLL `DesktopSticker.WallPaper.dll`，由既有 EXE 宿主通过 `LoadLibrary` 载入，接口为独立的一套 `IWallPaperModule` + `CreateWallPaperModule`/`DestroyWallPaperModule`（不复用 `IFeatureModule`）。纯策略逻辑写成 header-only 纯函数以便 `dtest` 直接单测，不依赖 D3D；OS 适配层（枚举盘符、全屏检测、进程执行）与策略分离。呈现路径统一：MF 或 FFmpeg 出帧 → D3D11 纹理 → DXGI 交换链 → DirectComposition visual → 壁纸窗口（WorkerW 子窗口、HWND_BOTTOM）。

**Tech Stack:** C++20 / MSVC v143 / MSBuild（无 cmake）、Win32、Direct3D 11、DXGI、DirectComposition、Media Foundation、FFmpeg（动态加载 avformat/avcodec/avutil/swscale）、nlohmann/json、dtest 自研测试框架、PowerShell（负载下载脚本）。

**Spec:** `docs/superpowers/specs/2026-09-25-dynamic-wallpaper-design.md`

## Global Constraints

- 构建**仅 Release x64**：`build.bat`（或 `build.bat test`）。本机 Debug CRT 环境异常，Debug 测试exe 会崩，一律不动 Debug。
- 无 cmake、无 globbing：**每个新增源文件/头文件都必须手动写进 `.vcxproj` 与 `.vcxproj.filters`**，否则不参与编译。
- 既有测试基线：**38 passed, 0 failed**。每个任务结束后该数字只能增加，不得回退。
- 运行测试：`cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && cp DesktopSticker.Features.dll Tests/ && ./Tests/DesktopSticker.Tests.exe`
- 提交信息：小写英文 `feat:` / `fix:` / `docs:` / `test:` 单行，行为导向（对齐既有风格）。
- 标识符英文、注释与 UI 文案中文。
- Features DLL 是 `DynamicLibrary` + `DESKTOPSTICKER_BUILD`→`__declspec(dllexport)`（见 `DesktopSticker.Features/include/desktopsticker/Export.h`）。WallPaper DLL 沿用同构做法。
- 产物目录：DLL → `$(SolutionDir)bin\$(Platform)\$(Configuration)\`；EXE → `$(SolutionDir)x64\Release\Desktop Sticker\`。EXE 的 `PostBuildEvent` 负责把依赖 DLL 复制到 EXE 旁。
- FFmpeg 负载**只动态加载，绝不静态链接**；不向 Windows 注册系统解码器。
- `tools/ffmpeg/` 二进制**不入 git**（进 `.gitignore`）。
- 壁纸功能任何初始化失败都必须降级为"壁纸不可用"，**不得影响分区收纳、搜索启动器、桌面时钟**。
- 新增的壁纸窗口**必须排除出"双击桌面空白"检测**，且 `WS_EX_NOACTIVATE` 不抢焦点。
- 不修改 Features 中 `DesktopShellIntegration.cpp` 的既有嵌入逻辑（历史上反复回归）。

## 文件结构总览

新增工程 `Desktop Sticker/DesktopSticker.WallPaper/`：

| 路径 | 职责 |
|---|---|
| `DesktopSticker.WallPaper.vcxproj` / `.filters` | 新工程；`DynamicLibrary`，OutDir 同 Features |
| `pch.h` / `pch.cpp` | 预编译头（WIN32_LEAN_AND_MEAN / NOMINMAX；windows.h、d3d11、dxgi、dcomp、mfapi、wincodec） |
| `include/desktopsticker/IWallPaperModule.h` | 对外唯一接口 + 结构体 + extern "C" 导出声明 |
| `include/desktopsticker/Export.h` | `DESKTOPSTICKER_WALLPAPER_API` 宏（是否导出；策略头不经此宏） |
| `include/desktopsticker/wallpaper/StoragePlacement.h` | **纯函数**：盘符选择（header-only） |
| `include/desktopsticker/wallpaper/PausePolicy.h` | **纯函数**：暂停优先级 reducer（header-only） |
| `include/desktopsticker/wallpaper/VariantPolicy.h` | **纯函数**：档位解析（header-only） |
| `include/desktopsticker/wallpaper/FrameScheduler.h` | **纯函数**：帧期限计算（header-only） |
| `include/desktopsticker/wallpaper/FfmpegCommand.h` | **纯函数**：转码命令行构造（header-only） |
| `include/desktopsticker/wallpaper/Types.h` | 共享枚举与结构体（VariantKind、WallPaperItem、WallPaperSettings 等） |
| `src/WallPaperModule.cpp` | `IWallPaperModule` 实现与导出工厂 |
| `src/WallPaperWindow.cpp/.h` | 壁纸窗口创建、WorkerW 嵌入、HWND_BOTTOM 维持、降级 |
| `src/DesktopHost.cpp/.h` | 壁纸宿主 WorkerW 发现（自包含，不依赖 Features） |
| `src/D3dContext.cpp/.h` | D3D11 设备 + DXGI 交换链 + DComp 目标 |
| `src/FrameSchedulerLoop.cpp/.h` | 专用线程 + 消息循环 + 帧调度 |
| `src/FfmpegApi.cpp/.h` | `LoadLibraryExW` 受限搜索加载 av*.dll 并解析符号 |
| `src/MediaFoundationDecoder.cpp/.h` | `IMFSourceReader` + DXGI 设备管理器 |
| `src/FfmpegTranscoder.cpp/.h` | `ffmpeg.exe` 子进程 + 串行队列 + 超时 |
| `src/ThumbnailGenerator.cpp/.h` | poster.png 生成 |
| `src/WallPaperStore.cpp/.h` | `wallpaper.json` / `library.json` 读写、原子替换、损坏容错 |
| `src/MediaLibrary.cpp/.h` | 导入（逐字节复制）、列表、重命名、删除 |
| `src/DriveInventory.cpp/.h` | OS 适配：枚举固定盘 + 卷序列号/根文件 ID 校验 |
| `src/FullscreenDetector.cpp/.h` | OS 适配：全屏遮挡枚举 + 会话/显示器状态 |

修改既有文件：

| 路径 | 改动 |
|---|---|
| `Desktop Sticker/Desktop Sticker.sln` | 加入新工程与配置映射 |
| `Desktop Sticker/Desktop Sticker/Desktop Sticker.vcxproj` | `ProjectReference` 新工程 + `PostBuildEvent` 复制 DLL 与 `ffmpeg\` |
| `Desktop Sticker/Desktop Sticker/Host.h` / `Host.cpp` | 新增 `LoadWallPaper`/`UnloadWallPaper` 并行路径 |
| `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj` | 加 WallPaper include 目录；加新测试文件 |
| `Desktop Sticker/Desktop Sticker/SettingsController.cpp/.h` | 新增"动态壁纸"卡片 |
| `README.md` / `AGENTS.md` | 新组件、负载脚本、许可声明说明 |
| `.gitignore` | `tools/ffmpeg/` |
| 新增 `tools/prepare_ffmpeg.ps1` | 固定版本 + SHA-256 下载脚本 |
| 新增 `third_party/*` 与根 `THIRD_PARTY_NOTICES.md` | 许可与声明 |

---

## Phase A — 骨架、负载与许可

### Task 1: 第三方许可与声明文件

**Files:**
- Create: `third_party/FFmpeg-NOTICE.txt`
- Create: `third_party/OpenH264-LICENSE.txt`
- Create: `third_party/MotionWallpaper-MIT.txt`
- Create: `THIRD_PARTY_NOTICES.md`（仓库根）
- Modify: `.gitignore`（新增 `tools/ffmpeg/`）
- Modify: `README.md`（新增"第三方组件与许可"章节）

**Interfaces:**
- Consumes: 无
- Produces: 三个 `third_party/` 声明文件；后续 Task 2 的脚本会复制其中两份到 `tools/ffmpeg/`。

- [ ] **Step 1: 写 `third_party/OpenH264-LICENSE.txt`**

内容为 Cisco OpenH264 的 BSD 许可全文（`Copyright (c) 2013, Cisco Systems`，三条款 BSD + 免责声明）。直接从参考仓库 `C:\Users\COLORFUL\AppData\Local\Temp\MotionWallpaper\third_party\OpenH264-LICENSE.txt` 复制，逐字不改。

- [ ] **Step 2: 写 `third_party/MotionWallpaper-MIT.txt`**

```
MotionWallpaper — https://github.com/1114656/MotionWallpaper
MIT License

Copyright (c) 2026 MotionWallpaper contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

本仓库参考其实现方式并移植部分策略层代码，故此声明必须保留。

- [ ] **Step 3: 写 `third_party/FFmpeg-NOTICE.txt`**

```
Desktop Sticker invokes an unmodified FFmpeg executable as a separate
background media-preparation tool. Its optional built-in playback fallback
also dynamically loads the accompanying unmodified avformat, avcodec,
avutil and swscale shared libraries through their public C interfaces.
No codec is registered in Windows and no FFmpeg library is statically linked
into Desktop Sticker. Compatible replacement shared libraries can be placed
in the ffmpeg directory next to the application; retain the required ABI and
dependent DLLs.

Build distribution: BtbN/FFmpeg-Builds, Windows x64 LGPL shared build 8.1
Project: https://github.com/BtbN/FFmpeg-Builds
Pinned build: autobuild-2026-09-16-19-44 / ffmpeg-n8.1.2-53-g1005b294ff-win64-lgpl-shared-8.1.zip
SHA-256: a654407793b1caef118550de3b99e46299dcabc6649ccf9a3a325f41ff4ea414
FFmpeg source and license information: https://ffmpeg.org/

The accompanying FFmpeg libraries are dynamically linked and distributed
under GNU LGPL v3.

This pinned FFmpeg build enables the OpenH264 encoder. OpenH264 is distributed
under its BSD license. This is a third-party FFmpeg build, not Cisco's official
precompiled OpenH264 binary, and no Cisco patent-license coverage is asserted
for it.
```

- [ ] **Step 4: 写 `THIRD_PARTY_NOTICES.md`（仓库根）**

以本项目的实际用法撰写，含四节：引言（本仓库原创代码的许可与第三方许可相互独立）、FFmpeg、OpenH264、MotionWallpaper。FFmpeg 与 OpenH264 两节使用规格 8.1 的措辞，并各自附"仓库内声明"路径。必须原样保留以下两段，不得弱化：

```
固定的 BtbN FFmpeg 构建启用了 Cisco OpenH264 编码器。OpenH264 源代码采用 BSD 许可证；发布负载保留其版权、条件和免责声明。
```

```
这里使用的是第三方 FFmpeg 构建中集成的 OpenH264，不是从 Cisco 官方下载的预编译 OpenH264 二进制；本声明不主张 Cisco 对官方预编译二进制提供的专利许可适用于该构建。正式分发前，发布者仍需独立确认适用地区的 H.264 专利许可要求。
```

- [ ] **Step 5: 更新 `.gitignore`**

在文件末尾追加：

```
# FFmpeg payload (fetched by tools/prepare_ffmpeg.ps1)
tools/ffmpeg/
```

- [ ] **Step 6: 更新 README**

在"图标版权"章节之后新增"第三方组件与许可"章节：说明 FFmpeg 以子进程与动态加载两种方式使用、LGPL v3、不静态链接；OpenH264 BSD 及其集成方式；`tools/prepare_ffmpeg.ps1` 获取负载、负载不入库；指向 `THIRD_PARTY_NOTICES.md`。

- [ ] **Step 7: 验证**

```bash
cd "D:/project/Desktop Sticker"
ls third_party/FFmpeg-NOTICE.txt third_party/OpenH264-LICENSE.txt third_party/MotionWallpaper-MIT.txt THIRD_PARTY_NOTICES.md
git check-ignore -v tools/ffmpeg/ffmpeg.exe   # 期望命中 tools/ffmpeg/ 规则
git status --short                            # 期望不含任何 tools/ffmpeg 下的二进制
```

- [ ] **Step 8: Commit**

```bash
git add .gitignore README.md THIRD_PARTY_NOTICES.md third_party/FFmpeg-NOTICE.txt third_party/OpenH264-LICENSE.txt third_party/MotionWallpaper-MIT.txt
git commit -m "docs: add third-party notices for FFmpeg, OpenH264 and MotionWallpaper"
```

---

### Task 2: FFmpeg 负载下载脚本

**Files:**
- Create: `tools/prepare_ffmpeg.ps1`

**Interfaces:**
- Consumes: `third_party/LICENSE-FFmpeg.txt`、`third_party/OpenH264-LICENSE.txt`（若 `LICENSE-FFmpeg.txt` 尚未存在，本任务同时创建，内容为 GNU LGPL v3 全文）
- Produces: `tools/ffmpeg/{ffmpeg.exe, av*.dll, LICENSE-FFmpeg.txt, LICENSE-OpenH264.txt}`，供 Task 3 的构建事件与运行期加载使用。

- [ ] **Step 1: 创建 `third_party/LICENSE-FFmpeg.txt`**

写入 GNU Lesser General Public License v3 全文（从 https://www.gnu.org/licenses/lgpl-3.0.txt 取得）。这是 LGPL 合规要求保留的许可正文。

- [ ] **Step 2: 写 `tools/prepare_ffmpeg.ps1`**

要求：
- 固定常量：zip 名 `ffmpeg-n8.1.2-53-g1005b294ff-win64-lgpl-shared-8.1.zip`、URL 指向 `https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-16-19-44/`、期望 SHA-256 `a654407793b1caef118550de3b99e46299dcabc6649ccf9a3a325f41ff4ea414`
- 目标目录由脚本自身位置推导：`$PSScriptRoot\ffmpeg`
- 已存在且校验通过则直接返回（幂等）
- 下载到临时文件 → `Get-FileHash -Algorithm SHA256` 比对 → **不匹配则删除并报错退出（非零退出码）**，绝不使用未校验的负载
- 解压后把 `bin\` 下的 `ffmpeg.exe` 与 `av*.dll`（另含 `swscale`/`swresample` 依赖）平铺复制到 `$PSScriptRoot\ffmpeg\`
- 把仓库 `third_party\LICENSE-FFmpeg.txt` 与 `third_party\OpenH264-LICENSE.txt` 复制为 `$PSScriptRoot\ffmpeg\LICENSE-FFmpeg.txt` 与 `LICENSE-OpenH264.txt`（不依赖压缩包内的文件名）
- 结束时打印负载目录与文件数量

- [ ] **Step 3: 验证脚本可运行且校验生效**

```bash
cd "D:/project/Desktop Sticker"
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_ffmpeg.ps1
ls tools/ffmpeg/ | head -20          # 期望看到 ffmpeg.exe、avcodec-*.dll、avformat-*.dll、avutil-*.dll、swscale-*.dll、LICENSE-*.txt
./tools/ffmpeg/ffmpeg.exe -version   # 期望输出 8.1 版本行
```

再人为篡改校验值（临时把期望 SHA-256 改成全 0）确认脚本**拒绝**并使用非零退出码退出，然后改回。此步证明校验不是摆设。

- [ ] **Step 4: 验证负载确实被 git 忽略**

```bash
git status --short          # 期望不出现 tools/ffmpeg 下任何文件
```

- [ ] **Step 5: Commit**

```bash
git add third_party/LICENSE-FFmpeg.txt tools/prepare_ffmpeg.ps1
git commit -m "feat(tools): pinned ffmpeg payload fetch script with sha256 verification"
```

---

### Task 3: WallPaper 工程骨架与对外接口

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/DesktopSticker.WallPaper.vcxproj`（+ `.filters`）
- Create: `Desktop Sticker/DesktopSticker.WallPaper/pch.h`、`pch.cpp`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/Export.h`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/Types.h`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/IWallPaperModule.h`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/WallPaperModule.cpp`
- Modify: `Desktop Sticker/Desktop Sticker.sln`
- Modify: `Desktop Sticker/Desktop Sticker/Desktop Sticker.vcxproj`（ProjectReference + PostBuildEvent）

**Interfaces:**
- Consumes: 无
- Produces:
  - `desktopsticker::VariantKind { Original, Balanced, PowerSaver }`
  - `desktopsticker::WallPaperItem { std::wstring id, name, sourceFile; bool hasPoster, hasBalanced, hasPowerSaver; uint64_t sourceBytes; }`
  - `desktopsticker::WallPaperSettings { bool enabled; std::wstring activeId; VariantKind preferred; bool pauseOnFullscreen, pauseOnLock; std::wstring libraryRoot; }`
  - `desktopsticker::WallPaperEvents { std::function<void()> libraryChanged; std::function<void()> playbackStateChanged; }`
  - `desktopsticker::IWallPaperModule`（方法签名见规格 3.2，后续任务按此实现）
  - 导出 `CreateWallPaperModule` / `DestroyWallPaperModule`

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
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
```

- [ ] **Step 2: 写 `Export.h` 与 `Types.h`**

`Export.h`：

```cpp
#pragma once

#ifdef DESKTOPSTICKER_WALLPAPER_BUILD
#define DESKTOPSTICKER_WALLPAPER_API __declspec(dllexport)
#else
#define DESKTOPSTICKER_WALLPAPER_API __declspec(dllimport)
#endif
```

`Types.h`：定义 `VariantKind`、`WallPaperItem`、`WallPaperSettings`（字段与默认值严格按规格 3.2）。

- [ ] **Step 3: 写 `IWallPaperModule.h`**

严格按规格 3.2 的接口清单实现，纯虚类 + 虚析构，末尾：

```cpp
extern "C" __declspec(dllexport) desktopsticker::IWallPaperModule* CreateWallPaperModule();
extern "C" __declspec(dllexport) void DestroyWallPaperModule(desktopsticker::IWallPaperModule*);
```

**注意**：该头文件不得 include `pch.h` 或任何 D3D/MF 头；它必须能只靠 `<functional>`/`<string>`/`<vector>`/`<cstdint>` 被 EXE 侧编译。`HICON` 用前向声明或 `<windows.h>`（EXE 侧已有）。

- [ ] **Step 4: 写 `src/WallPaperModule.cpp` 最小实现**

只做骨架：`Init` 存事件、`Start` 返回 `true`、`Stop`/`Shutdown` 空、库操作返回空/false、`GetSettings` 返回默认值。工厂函数 `new`/`delete`。此任务**不接任何真实逻辑**。

- [ ] **Step 5: 写 vcxproj**

照 `DesktopSticker.Features.vcxproj` 同构：
- `ConfigurationType = DynamicLibrary`
- `TargetName = DesktopSticker.WallPaper`
- `OutDir = $(SolutionDir)bin\$(Platform)\$(Configuration)\`
- `IntDir = $(SolutionDir)obj\$(Platform)\$(Configuration)\WallPaper\`
- 预处理宏 `DESKTOPSTICKER_WALLPAPER_BUILD`
- `AdditionalIncludeDirectories` 含 `$(ProjectDir)include;$(ProjectDir)..\..\third_party`
- `AdditionalDependencies`：`d3d11.lib;dxgi.lib;dcomp.lib;mfplat.lib;mf.lib;mfuuid.lib;windowscodecs.lib;ole32.lib;shlwapi.lib;shell32.lib;uuid.lib`
- 语言标准 `stdcpp20`，`PrecompiledHeader = Use` / `pch.h`
- 显式列出 `pch.cpp`、`src\WallPaperModule.cpp` 的 `ClCompile`，以及全部 `ClInclude`
- 手动维护 `.vcxproj.filters`

- [ ] **Step 6: 加入解决方案并配置引用与构建事件**

```
sln：加入 DesktopSticker.WallPaper 工程（GUID {8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}），
     补齐 Release|x64 的 .Build.0 与配置映射。
EXE vcxproj：<ProjectReference Include="..\DesktopSticker.WallPaper\DesktopSticker.WallPaper.vcxproj" />
PostBuildEvent 增加：
  xcopy /Y /D "$(SolutionDir)bin\$(Platform)\$(Configuration)\DesktopSticker.WallPaper.dll" "$(OutDir)"
  xcopy /Y /D "$(SolutionDir)..\tools\ffmpeg\*.dll" "$(OutDir)ffmpeg\" 2>nul
  xcopy /Y /D "$(SolutionDir)..\tools\ffmpeg\ffmpeg.exe" "$(OutDir)ffmpeg\" 2>nul
  xcopy /Y /D "$(SolutionDir)..\tools\ffmpeg\LICENSE-*.txt" "$(OutDir)ffmpeg\" 2>nul
```

后三条在负载缺失时**不得让构建失败**（故加 `2>nul` 且不加 errorlevel 检查）。

- [ ] **Step 7: 编译验证**

```bash
cd "D:/project/Desktop Sticker"
cmd //c build.bat
ls "Desktop Sticker/bin/x64/Release/DesktopSticker.WallPaper.dll"
ls "Desktop Sticker/x64/Release/Desktop Sticker/DesktopSticker.WallPaper.dll"   # PostBuildEvent 已复制
ls "Desktop Sticker/x64/Release/Desktop Sticker/ffmpeg/ffmpeg.exe"             # 负载已复制
```

- [ ] **Step 8: 跑既有测试确认无回归**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && cp DesktopSticker.Features.dll Tests/ && ./Tests/DesktopSticker.Tests.exe
```
Expected: `38 passed, 0 failed`

- [ ] **Step 9: Commit**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper" "Desktop Sticker/Desktop Sticker.sln" "Desktop Sticker/Desktop Sticker/Desktop Sticker.vcxproj"
git commit -m "feat(wallpaper): scaffold DesktopSticker.WallPaper dll with IWallPaperModule interface"
```

---

### Task 4: Host 并行加载 WallPaper 模块（含降级）

**Files:**
- Modify: `Desktop Sticker/Desktop Sticker/Host.h`
- Modify: `Desktop Sticker/Desktop Sticker/Host.cpp`

**Interfaces:**
- Consumes: Task 3 的 `IWallPaperModule.h` 与 DLL
- Produces: `Host::LoadWallPaper()`、`Host::UnloadWallPaper()`、`Host::WallPaper()`（返回 `IWallPaperModule*`，可能为 `nullptr`）

- [ ] **Step 1: 扩展 `Host.h`**

新增私有成员 `HMODULE wpDll_`、`desktopsticker::IWallPaperModule* wpModule_`，以及公开方法：

```cpp
bool LoadWallPaper();          // 失败返回 false，但不影响 Features
void UnloadWallPaper();
desktopsticker::IWallPaperModule* WallPaper() const { return wpModule_; }
void SetWallPaperEvents(std::function<void()> libraryChanged,
                        std::function<void()> playbackStateChanged);
```

- [ ] **Step 2: 实现 `LoadWallPaper`，照 `LoadFeatures` 同构**

要求：
- 从 `GetModuleFileNameW(nullptr)` 推 EXE 目录，加载 `DesktopSticker.WallPaper.dll`
- `GetProcAddress` 取 `CreateWallPaperModule` / `DestroyWallPaperModule`
- 构造 `WallPaperEvents` 并 `Init`
- 任何失败（文件不存在 / LoadLibrary 失败 / 符号缺失 / Create 返回空 / Init 失败）→ 收干净资源、`wpModule_ = nullptr`、返回 `false`，**不抛异常到调用方**
- 复用既有 try/catch 收尾写法，不引入泛型抽象

- [ ] **Step 3: 实现 `UnloadWallPaper`**

先 `Shutdown()`，再 `DestroyWallPaperModule`，最后 `FreeLibrary`；顺序与 `UnloadFeatures` 一致。

- [ ] **Step 4: 在 App 启动路径调用（不阻断启动）**

在 `LoadFeatures` 成功后调用 `LoadWallPaper()`；返回值**只记录日志**，不弹错误、不中断启动。日志走既有 `AppLog`（见 `Desktop Sticker/Desktop Sticker/AppLog.h`）。

- [ ] **Step 5: 编译并验证降级路径**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat
```

验证 A：DLL 存在 → 启动应用，日志出现 WallPaper 加载成功；分区/时钟正常。
验证 B：临时把 `DesktopSticker.WallPaper.dll` 改名为 `.bak` → 启动应用，日志出现加载失败，**分区、双击空格搜索、时钟全部正常**；改回。

- [ ] **Step 6: 跑测试 + Commit**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && ./Tests/DesktopSticker.Tests.exe   # 38 passed
cd "D:/project/Desktop Sticker"
git add "Desktop Sticker/Desktop Sticker/Host.h" "Desktop Sticker/Desktop Sticker/Host.cpp"
git commit -m "feat(wallpaper): load wallpaper module from host with graceful degradation"
```

---

## Phase B — 纯策略（header-only，TDD）

> 本阶段全部是 header-only 纯函数，**不依赖 D3D/MF**，因此测试只需 `#include` 头文件即可。Task 5 负责把 include 目录接进测试工程，后续任务直接复用。
> 参考项目的做法：把策略提炼成纯函数头再测（见其 `DesktopHostPolicy.h`、`IdlePolicy.h`）。

### Task 5: 测试工程接入 + StoragePlacement 盘符选择

**Files:**
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`（include 目录 + 新 ClCompile）
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/StoragePlacement.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperStoragePlacement.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `desktopsticker::wallpaper::DriveKind { Fixed, Removable, Remote, Optical, Ram, Unknown }`
  - `desktopsticker::wallpaper::DriveInfo { wchar_t letter; DriveKind kind; uint64_t freeAvailable; }`
  - `std::optional<wchar_t> choose_library_drive(const std::vector<DriveInfo>&)`

- [ ] **Step 1: 测试工程加 include 目录**

在 `DesktopSticker.Tests.vcxproj` 两处 `AdditionalIncludeDirectories`（Debug/Release）追加：
`$(ProjectDir)..\DesktopSticker.WallPaper\include;`

**不需要**加 ProjectReference 或链接 lib —— 策略是 header-only，测试只 include 头文件。

- [ ] **Step 2: 写失败测试 `TestWallPaperStoragePlacement.cpp`**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/StoragePlacement.h>

using namespace desktopsticker::wallpaper;

TEST(StoragePlacement_ExcludesNonFixedDrives) {
    std::vector<DriveInfo> drives = {
        {'C', DriveKind::Fixed,     100ull},
        {'D', DriveKind::Removable, 999999ull},   // 大容量移动盘，必须排除
        {'E', DriveKind::Remote,    888888ull},
        {'F', DriveKind::Optical,   777777ull},
    };
    auto picked = choose_library_drive(drives);
    ASSERT_TRUE(picked.has_value());
    ASSERT_EQ(L'C', *picked);
}

TEST(StoragePlacement_PicksLargestFreeSpace) {
    std::vector<DriveInfo> drives = {
        {'C', DriveKind::Fixed, 10ull},
        {'D', DriveKind::Fixed, 500ull},
        {'E', DriveKind::Fixed, 20ull},
    };
    auto picked = choose_library_drive(drives);
    ASSERT_TRUE(picked.has_value());
    ASSERT_EQ(L'D', *picked);
}

TEST(StoragePlacement_TieBreaksByLowestLetter) {
    std::vector<DriveInfo> drives = {
        {'F', DriveKind::Fixed, 300ull},
        {'D', DriveKind::Fixed, 300ull},
        {'C', DriveKind::Fixed, 300ull},
    };
    auto picked = choose_library_drive(drives);
    ASSERT_TRUE(picked.has_value());
    ASSERT_EQ(L'C', *picked);
}

TEST(StoragePlacement_NoFixedDrive_ReturnsNullopt) {
    std::vector<DriveInfo> drives = {
        {'D', DriveKind::Removable, 999999ull},
        {'E', DriveKind::Remote,    888888ull},
    };
    ASSERT_FALSE(choose_library_drive(drives).has_value());
}

TEST(StoragePlacement_EmptyInput_ReturnsNullopt) {
    ASSERT_FALSE(choose_library_drive({}).has_value());
}
```

- [ ] **Step 3: 把新测试文件加进 vcxproj 并运行，确认失败**

在 `DesktopSticker.Tests.vcxproj` 加 `<ClCompile Include="TestWallPaperStoragePlacement.cpp" />`，然后：

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: 编译失败（找不到 `StoragePlacement.h`）。

- [ ] **Step 4: 写实现 `StoragePlacement.h`**

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace desktopsticker::wallpaper {

enum class DriveKind { Fixed, Removable, Remote, Optical, Ram, Unknown };

struct DriveInfo {
    wchar_t letter = 0;          // 'C'
    DriveKind kind = DriveKind::Unknown;
    uint64_t freeAvailable = 0;  // GetDiskFreeSpaceExW 的 ullFreeAvailable
};

// 只考虑固定盘（排除可移动盘/网络盘/光驱），取可用空间最大者；
// 并列时取盘符最小者以保证结果确定。无合格固定盘返回 nullopt。
inline std::optional<wchar_t> choose_library_drive(const std::vector<DriveInfo>& drives) {
    std::optional<wchar_t> best;
    uint64_t bestFree = 0;
    for (const auto& d : drives) {
        if (d.kind != DriveKind::Fixed) continue;
        const bool better = !best.has_value() || d.freeAvailable > bestFree ||
                            (d.freeAvailable == bestFree && d.letter < *best);
        if (better) {
            best = d.letter;
            bestFree = d.freeAvailable;
        }
    }
    return best;
}

} // namespace desktopsticker::wallpaper
```

- [ ] **Step 5: 运行测试，确认通过且总数 43**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: `43 passed, 0 failed`（38 + 5）

- [ ] **Step 6: Commit**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/StoragePlacement.h" "Desktop Sticker/DesktopSticker.Tests"
git commit -m "feat(wallpaper): library drive selection policy with unit tests"
```

---

### Task 6: PausePolicy 暂停优先级

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/PausePolicy.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperPausePolicy.cpp`
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: 无
- Produces:
  - `PauseReason { None, SessionLocked, DisplayOff, UserPaused, FullscreenCovered }`
  - `PauseInputs { bool sessionLocked, displayOff, userPaused, fullscreenCovered; }`
  - `PlaybackState { Playing, Paused }`
  - `PauseDecision { PlaybackState state; PauseReason reason; }`
  - `PauseDecision reduce_pause_policy(const PauseInputs&)`

- [ ] **Step 1: 写失败测试 `TestWallPaperPausePolicy.cpp`**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/PausePolicy.h>

using namespace desktopsticker::wallpaper;

TEST(PausePolicy_AllClear_Plays) {
    auto d = reduce_pause_policy(PauseInputs{});
    ASSERT_TRUE(d.state == PlaybackState::Playing);
    ASSERT_TRUE(d.reason == PauseReason::None);
}

TEST(PausePolicy_SessionLockedBeatsEverything) {
    PauseInputs in{};
    in.sessionLocked = true;
    in.displayOff = true;
    in.userPaused = true;
    in.fullscreenCovered = true;
    auto d = reduce_pause_policy(in);
    ASSERT_TRUE(d.state == PlaybackState::Paused);
    ASSERT_TRUE(d.reason == PauseReason::SessionLocked);
}

TEST(PausePolicy_DisplayOffBeatsUserPauseAndFullscreen) {
    PauseInputs in{};
    in.displayOff = true;
    in.userPaused = true;
    in.fullscreenCovered = true;
    ASSERT_TRUE(reduce_pause_policy(in).reason == PauseReason::DisplayOff);
}

TEST(PausePolicy_UserPauseBeatsFullscreen) {
    PauseInputs in{};
    in.userPaused = true;
    in.fullscreenCovered = true;
    ASSERT_TRUE(reduce_pause_policy(in).reason == PauseReason::UserPaused);
}

TEST(PausePolicy_FullscreenAlonePauses) {
    PauseInputs in{};
    in.fullscreenCovered = true;
    auto d = reduce_pause_policy(in);
    ASSERT_TRUE(d.state == PlaybackState::Paused);
    ASSERT_TRUE(d.reason == PauseReason::FullscreenCovered);
}
```

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: 编译失败（找不到 `PausePolicy.h`）

- [ ] **Step 3: 写实现 `PausePolicy.h`**

```cpp
#pragma once

namespace desktopsticker::wallpaper {

enum class PauseReason { None, SessionLocked, DisplayOff, UserPaused, FullscreenCovered };
enum class PlaybackState { Playing, Paused };

struct PauseInputs {
    bool sessionLocked = false;
    bool displayOff = false;
    bool userPaused = false;
    bool fullscreenCovered = false;
};

struct PauseDecision {
    PlaybackState state = PlaybackState::Playing;
    PauseReason reason = PauseReason::None;
};

// 固定优先级：会话锁定 > 显示器关闭 > 用户手动暂停 > 全屏遮挡 > 正常播放。
inline PauseDecision reduce_pause_policy(const PauseInputs& in) {
    if (in.sessionLocked)     return {PlaybackState::Paused, PauseReason::SessionLocked};
    if (in.displayOff)        return {PlaybackState::Paused, PauseReason::DisplayOff};
    if (in.userPaused)        return {PlaybackState::Paused, PauseReason::UserPaused};
    if (in.fullscreenCovered) return {PlaybackState::Paused, PauseReason::FullscreenCovered};
    return {PlaybackState::Playing, PauseReason::None};
}

} // namespace desktopsticker::wallpaper
```

- [ ] **Step 4: 运行测试，确认 48 passed**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: `48 passed, 0 failed`

- [ ] **Step 5: Commit**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/PausePolicy.h" "Desktop Sticker/DesktopSticker.Tests"
git commit -m "feat(wallpaper): pause priority reducer with unit tests"
```

---

### Task 7: VariantPolicy 档位解析

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/VariantPolicy.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperVariantPolicy.cpp`
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: `VariantKind`（`Types.h`，Task 3）；测试侧因 header-only 需要该枚举，故测试直接 include `<desktopsticker/wallpaper/Types.h>` 或由本头文件自带一份最小枚举——**采用前者**，`Types.h` 必须无 D3D 依赖
- Produces:
  - `VariantAvailability { bool hasBalanced, hasPowerSaver; }`
  - `VariantKind resolve_effective_variant(VariantKind preferred, const VariantAvailability&)`

- [ ] **Step 1: 确认 `Types.h` 可被测试工程独立 include**

`Types.h` 只允许依赖 `<cstdint>`/`<string>`。若 Task 3 误加了 windows/D3D 依赖，此步修正。

- [ ] **Step 2: 写失败测试**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/VariantPolicy.h>

using namespace desktopsticker::wallpaper;

TEST(VariantPolicy_OriginalAlwaysOriginal) {
    VariantAvailability a{true, true};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::Original, a) == VariantKind::Original);
}

TEST(VariantPolicy_BalancedWhenPresent) {
    VariantAvailability a{true, false};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::Balanced, a) == VariantKind::Balanced);
}

TEST(VariantPolicy_BalancedMissing_FallsBackToOriginal) {
    VariantAvailability a{false, true};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::Balanced, a) == VariantKind::Original);
}

TEST(VariantPolicy_PowerSaverWhenPresent) {
    VariantAvailability a{false, true};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::PowerSaver, a) == VariantKind::PowerSaver);
}

TEST(VariantPolicy_PowerSaverMissing_FallsBackToOriginal) {
    VariantAvailability a{true, false};
    ASSERT_TRUE(resolve_effective_variant(VariantKind::PowerSaver, a) == VariantKind::Original);
}
```

- [ ] **Step 3: 加入 vcxproj，运行确认失败**

Expected: 编译失败（找不到 `VariantPolicy.h`）

- [ ] **Step 4: 写实现**

```cpp
#pragma once
#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

struct VariantAvailability {
    bool hasBalanced = false;
    bool hasPowerSaver = false;
};

// 首选档位的副本存在则用它，否则回落到原画（原画路径再由 MF→FFmpeg 兜底）。
inline VariantKind resolve_effective_variant(VariantKind preferred,
                                             const VariantAvailability& a) {
    if (preferred == VariantKind::Balanced && a.hasBalanced) return VariantKind::Balanced;
    if (preferred == VariantKind::PowerSaver && a.hasPowerSaver) return VariantKind::PowerSaver;
    return VariantKind::Original;
}

} // namespace desktopsticker::wallpaper
```

- [ ] **Step 5: 运行测试，确认 53 passed**

- [ ] **Step 6: Commit**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/VariantPolicy.h" "Desktop Sticker/DesktopSticker.Tests"
git commit -m "feat(wallpaper): variant selection policy with unit tests"
```

---

### Task 8: FrameScheduler 帧期限计算

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/FrameScheduler.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperFrameScheduler.cpp`
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: 无
- Produces:
  - `FrameScheduleDecision { int64_t waitHundredNs; bool skipBacklog; }`
  - `int64_t next_deadline(int64_t previousDeadline, int64_t frameDuration)`
  - `FrameScheduleDecision schedule_frame(int64_t nowQpc, int64_t deadlineQpc)`

- [ ] **Step 1: 写失败测试**

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/FrameScheduler.h>

using namespace desktopsticker::wallpaper;

TEST(FrameScheduler_AheadOfDeadline_WaitsRemaining) {
    auto d = schedule_frame(1000, 3000);
    ASSERT_FALSE(d.skipBacklog);
    ASSERT_EQ(2000ll, d.waitHundredNs);
}

TEST(FrameScheduler_ExactlyAtDeadline_NoWait) {
    auto d = schedule_frame(3000, 3000);
    ASSERT_FALSE(d.skipBacklog);
    ASSERT_EQ(0ll, d.waitHundredNs);
}

TEST(FrameScheduler_PastDeadline_SkipsBacklog) {
    auto d = schedule_frame(5000, 3000);
    ASSERT_TRUE(d.skipBacklog);
    ASSERT_EQ(0ll, d.waitHundredNs);
}

TEST(FrameScheduler_NextDeadlineAdvances) {
    ASSERT_EQ(9000ll, next_deadline(3000, 6000));
}

TEST(FrameScheduler_NextDeadlineAfterBacklog_DropsMissedFrames) {
    // 当前 20000，帧长 1000，前一期限 3000 → 下一个未错过的期限是 21000
    ASSERT_EQ(21000ll, next_deadline_not_before(3000, 1000, 20000));
}
```

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

Expected: 编译失败（找不到 `FrameScheduler.h`）

- [ ] **Step 3: 写实现**

```cpp
#pragma once
#include <cstdint>

namespace desktopsticker::wallpaper {

// 时间单位统一为 QPC 的 100ns，避免浮点与单位混用。
struct FrameScheduleDecision {
    int64_t waitHundredNs = 0;
    bool skipBacklog = false;
};

inline FrameScheduleDecision schedule_frame(int64_t nowQpc, int64_t deadlineQpc) {
    if (nowQpc < deadlineQpc) return {deadlineQpc - nowQpc, false};
    return {0, true};
}

inline int64_t next_deadline(int64_t previousDeadline, int64_t frameDuration) {
    return previousDeadline + frameDuration;
}

// 积压后不追赶：返回第一个不早于 now 的期限，丢弃已错过的帧。
inline int64_t next_deadline_not_before(int64_t previousDeadline,
                                        int64_t frameDuration,
                                        int64_t nowQpc) {
    if (frameDuration <= 0) return nowQpc;
    int64_t next = previousDeadline;
    if (next <= nowQpc) {
        const int64_t missed = (nowQpc - next) / frameDuration;
        next += (missed + 1) * frameDuration;
    }
    return next;
}

} // namespace desktopsticker::wallpaper
```

- [ ] **Step 4: 运行测试，确认 58 passed**

- [ ] **Step 5: Commit**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/FrameScheduler.h" "Desktop Sticker/DesktopSticker.Tests"
git commit -m "feat(wallpaper): frame deadline scheduling policy with unit tests"
```

---

### Task 9: FfmpegCommand 转码命令行构造

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/FfmpegCommand.h`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperFfmpegCommand.cpp`
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: `VariantKind`（`Types.h`）
- Produces:
  - `std::vector<std::wstring> build_transcode_args(const std::wstring& input, const std::wstring& output, VariantKind kind)`
  - `std::wstring variant_file_name(VariantKind kind, int revision)`（如 `balanced-v1.mp4`）

- [ ] **Step 1: 写失败测试**

断言要点（**不改写源文件**是硬约束，必须被测试锁住）：

```cpp
#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/FfmpegCommand.h>

using namespace desktopsticker::wallpaper;

static bool contains(const std::vector<std::wstring>& v, const std::wstring& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// 取某个开关后面紧跟的参数值；未找到返回空串。
static std::wstring arg_after(const std::vector<std::wstring>& v, const std::wstring& flag) {
    auto it = std::find(v.begin(), v.end(), flag);
    if (it == v.end() || it + 1 == v.end()) return L"";
    return *(it + 1);
}

static int crf_of(const std::vector<std::wstring>& v) {
    const std::wstring s = arg_after(v, L"-crf");
    return s.empty() ? -1 : std::wcstol(s.c_str(), nullptr, 10);
}

TEST(FfmpegCommand_InputIsSource_OutputIsVariant) {
    auto a = build_transcode_args(L"C:\\x\\source.mp4", L"D:\\lib\\balanced-v1.mp4",
                                  VariantKind::Balanced);
    ASSERT_TRUE(contains(a, L"-i"));
    ASSERT_STREQ(L"C:\\x\\source.mp4", arg_after(a, L"-i"));      // 输入是源文件
    ASSERT_STREQ(L"D:\\lib\\balanced-v1.mp4", a.back());          // 输出是派生副本
}

TEST(FfmpegCommand_NeverWritesBackToSource) {
    auto a = build_transcode_args(L"C:\\x\\source.mp4", L"D:\\lib\\balanced-v1.mp4",
                                  VariantKind::Balanced);
    // 源路径只能作为 -i 的取值出现，绝不能成为输出
    ASSERT_TRUE(a.back() != L"C:\\x\\source.mp4");
}

TEST(FfmpegCommand_NoAudioTrack) {
    auto a = build_transcode_args(L"a.mp4", L"b.mp4", VariantKind::Balanced);
    ASSERT_TRUE(contains(a, L"-an"));            // 壁纸静音，不输出音轨
}

TEST(FfmpegCommand_PowerSaverIsSmallerThanBalanced) {
    auto bal = build_transcode_args(L"a.mp4", L"b.mp4", VariantKind::Balanced);
    auto pwr = build_transcode_args(L"a.mp4", L"c.mp4", VariantKind::PowerSaver);
    ASSERT_TRUE(crf_of(pwr) > crf_of(bal));      // 省电档 CRF 更大 = 码率更低
}

TEST(FfmpegCommand_OriginalHasNoTranscodeArgs) {
    ASSERT_TRUE(build_transcode_args(L"a.mp4", L"b.mp4", VariantKind::Original).empty());
}

TEST(FfmpegCommand_VariantFileName) {
    ASSERT_STREQ(L"balanced-v1.mp4", variant_file_name(VariantKind::Balanced, 1));
    ASSERT_STREQ(L"power-saver-v2.mp4", variant_file_name(VariantKind::PowerSaver, 2));
}
```

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

- [ ] **Step 3: 写实现**

```cpp
#pragma once
#include <string>
#include <vector>
#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

inline std::wstring variant_file_name(VariantKind kind, int revision) {
    const wchar_t* prefix = (kind == VariantKind::PowerSaver) ? L"power-saver" : L"balanced";
    return std::wstring(prefix) + L"-v" + std::to_wstring(revision) + L".mp4";
}

// 生成性能副本的 ffmpeg 参数。恒定输出到派生文件，绝不指向源文件。
inline std::vector<std::wstring> build_transcode_args(const std::wstring& input,
                                                      const std::wstring& output,
                                                      VariantKind kind) {
    if (kind == VariantKind::Original) return {};

    const int crf = (kind == VariantKind::PowerSaver) ? 28 : 23;
    return {
        L"-hide_banner", L"-loglevel", L"error",
        L"-nostdin",
        L"-y",                                   // 只覆盖我们自己的派生文件
        L"-i", input,
        L"-c:v", L"libx264",
        L"-preset", L"veryfast",
        L"-crf", std::to_wstring(crf),
        L"-pix_fmt", L"yuv420p",
        L"-movflags", L"+faststart",
        L"-an",                                  // 壁纸静音
        output,
    };
}

} // namespace desktopsticker::wallpaper
```

- [ ] **Step 4: 运行测试，确认 64 passed**

- [ ] **Step 5: Commit**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/FfmpegCommand.h" "Desktop Sticker/DesktopSticker.Tests"
git commit -m "feat(wallpaper): ffmpeg transcode argument builder with unit tests"
```

---

## Phase C — 存储与媒体库

### Task 10: WallPaperStore 持久化

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/WallPaperStore.h`、`.cpp`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperStore.cpp`
- Modify: `DesktopSticker.WallPaper.vcxproj`、`DesktopSticker.Tests.vcxproj`

**Interfaces:**
- Consumes: `WallPaperSettings`、`WallPaperItem`（`Types.h`）
- Produces:
  - `WallPaperStore::LoadSettings(path)` / `SaveSettings(path, const WallPaperSettings&)` —— 损坏时返回默认值并备份原文件为 `wallpaper.json.bak`
  - `WallPaperStore::LoadLibrary(path)` / `SaveLibrary(path, const std::vector<WallPaperItem>&)` —— 同上
  - 写入一律"临时文件 + 原子替换"
- 测试策略：本单元逻辑可测（JSON 往返 + 损坏容错），把路径作为参数以便测试用临时目录。**不引入新测试框架**。

- [ ] **Step 1: 写失败测试**（覆盖：设置往返、库往返、损坏 JSON 不崩溃且回落默认、缺失文件回落默认、含未知字段不丢已知字段）

要点：断言损坏输入**不抛异常**、返回默认值、且原文件被备份。

- [ ] **Step 2: 加入两个 vcxproj，运行确认失败**

注意：本任务起测试需要**链接** WallPaper 的实现（非 header-only），因此 `DesktopSticker.Tests.vcxproj` 需加 `ProjectReference` 到 WallPaper 工程 + `AdditionalDependencies` 加 `DesktopSticker.WallPaper.lib`。策略头仍不需链接。

- [ ] **Step 3: 实现 `WallPaperStore`**

要求：用 `nlohmann/json`（include 根已是 `third_party`）；`library.json` 带 `version` 字段供后续迁移；所有写入走临时文件 + `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`；损坏时 `MoveFileExW` 备份为 `.bak` 后返回默认。

- [ ] **Step 4: 运行测试，确认通过**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(wallpaper): wallpaper and library json store with corruption tolerance"
```

---

### Task 11: MediaLibrary 导入与增删改

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/MediaLibrary.h`、`.cpp`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperMediaLibrary.cpp`
- Modify: 两个 vcxproj

**Interfaces:**
- Consumes: `WallPaperStore`
- Produces:
  - `MediaLibrary::Import(root, srcPath, outId)` —— 逐字节复制到 `media\<id>\source.<ext>`；`<id>` 为新建 GUID 字符串
  - `MediaLibrary::List(root)`、`Rename(root, id, name)`、`Remove(root, id)`（**只删库内副本，绝不删用户原文件**）
  - `MediaLibrary::ItemDir(root, id)`、`SourcePath(root, id)`

- [ ] **Step 1: 写失败测试**（临时目录内）

必须锁住的硬约束：
- 导入后源文件**字节完全不变、且仍存在**（读源文件 hash 前后比对）
- 导入后 `source.<ext>` 与源文件字节一致
- `Remove` 后库内条目目录消失，**用户原文件仍在**
- `List` 能读回名称与大小
- 名称缺省为源文件主名

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

- [ ] **Step 3: 实现 `MediaLibrary`**

要求：复制用流式块复制（大文件不整读进内存）；扩展名取自源文件；`Remove` 只递归删 `media\<id>\` 且校验该路径确实位于库根之下（防止路径逃逸）。

- [ ] **Step 4: 运行测试，确认通过**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(wallpaper): media library import, rename and remove with source-file safety"
```

---

### Task 12: 磁盘清单适配层（OS 适配 + 卷标识校验）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/DriveInventory.h`、`.cpp`
- Create: `Desktop Sticker/DesktopSticker.Tests/TestWallPaperDriveIdentity.cpp`
- Modify: 两个 vcxproj

**Interfaces:**
- Consumes: `choose_library_drive`（Header-only 策略）
- Produces:
  - `std::vector<DriveInfo> enumerate_drives()` —— `GetLogicalDriveStringsW` + `GetDriveTypeW` + `GetDiskFreeSpaceExW`
  - `VolumeIdentity { DWORD serial; uint64_t rootFileId; }`
  - `bool probe_volume_identity(const std::wstring& root, VolumeIdentity& out)` —— `GetVolumeInformationW` + `GetFileInformationByHandle`
  - `bool verify_volume_identity(const std::wstring& root, const VolumeIdentity& expected)` —— 启动校验，防盘符被复用
  - `std::optional<std::wstring> resolve_library_root()` —— 组合：枚举 → 选择 → 建目录 → 返回根路径

- [ ] **Step 1: 写失败测试**

可纯测的部分：`DriveKind` 到 `DriveInfo` 的映射函数 `map_drive_type(UINT win32Type)`（把 `DRIVE_FIXED`→`Fixed`、`DRIVE_REMOVABLE`→`Removable`、`DRIVE_REMOTE`→`Remote`、`DRIVE_CDROM`→`Optical`、`DRIVE_RAMDISK`→`Ram`、未知→`Unknown`）。`GetDriveTypeW` 本身不测（OS 依赖）。

再加一个**同目录内**的体积校验往返测试：对当前工作目录 `probe_volume_identity` 后再 `verify_volume_identity` 应为 true；对伪造的 serial 应为 false。

- [ ] **Step 2: 加入 vcxproj，运行确认失败**

- [ ] **Step 3: 实现**

要求：`rootFileId` 由 `GetFileInformationByHandle` 的 `nFileIndexHigh/Low` 组合；`verify` 必须同时比对 serial 与 fileId；`resolve_library_root` 在无合格固定盘时返回 `nullopt`（调用方降级）。

- [ ] **Step 4: 运行测试，确认通过**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(wallpaper): drive inventory and volume identity verification"
```

---

## Phase D — 呈现管线（风险最高，人工验证为主）

### Task 13: D3D11 + DXGI + DirectComposition 上屏 + 桌面嵌入

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/DesktopHost.h`、`.cpp`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/D3dContext.h`、`.cpp`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/WallPaperWindow.h`、`.cpp`
- Modify: `DesktopSticker.WallPaper.vcxproj`

**Interfaces:**
- Consumes: 无（本任务不接解码）
- Produces:
  - `DesktopHost::FindWallpaperWorkerW()` → `HWND`（DefView 之后那个 WorkerW）；失败返回 `nullptr`
  - `D3dContext::Create(HWND, width, height)` → `bool`；`Present(ID3D11Texture2D*)`；持有 D3D11 设备、DXGI 交换链、`IDCompositionDevice`/`Target`/`Visual`
  - `WallPaperWindow::Create()` → 创建窗口、尝试嵌入、返回是否成功嵌入；`PlaceAtBottom()`

**这是全项目风险最集中的任务**（DComp 挂在 WorkerW 子窗口上本项目从未做过）。必须先跑通最小可见效果，再往上叠解码。

- [ ] **Step 1: 实现 `DesktopHost::FindWallpaperWorkerW`**

自包含实现（**不 include Features 的任何头**），逻辑对齐既有 `DesktopShellIntegration.cpp`：
- `FindWindowW(L"Progman", nullptr)` → 发未文档化消息 `0x052C` 促使生成 WorkerW
- `EnumWindows` 找持有 `SHELLDLL_DefView` 的 WorkerW；其**之后**的兄弟 WorkerW 即壁纸宿主
- 兼容 `Progman` 直接持有 `SHELLDLL_DefView` 的旧路径

- [ ] **Step 2: 实现 `WallPaperWindow`**

- 窗口类样式：`WS_POPUP`，扩展样式 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，**不加** `WS_EX_TRANSPARENT`（历史上透明命中测试破坏输入）
- 创建后 `SetParent(hwnd, hostWorkerW)`，然后 `SetWindowPos(hwnd, HWND_BOTTOM, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW)`
- 覆盖主显示器工作区（`GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`；本期单屏主显示器）
- 不处理 `WM_MOUSEACTIVATE`；对 `WM_NCHITTEST` 返回 `HTTRANSPARENT` 使其不截点击
- `PlaceAtBottom()` 供分区变更后重申层级
- 嵌入失败（`SetParent` 后父窗口不符）→ 降级：不 `SetParent`，改为 `SetWindowPos(HWND_BOTTOM)` 的普通置底窗口

- [ ] **Step 3: 实现 `D3dContext`**

- `D3D11CreateDevice`（`D3D_DRIVER_TYPE_HARDWARE`，失败回退 `WARP`）
- `IDXGIFactory2::CreateSwapChainForComposition`（`DXGI_FORMAT_B8G8R8A8_UNORM`，`DXGI_SWAP_EFFECT_FLIP_DISCARD`，`DXGI_ALPHA_MODE_IGNORE`）
- `DCompositionCreateDevice(dxgiDevice)` → `CreateTargetForHwnd(hwnd, TRUE)` → `CreateVisual()` → `SetContent(swapChain)` → `SetRoot` → `Commit`
- `Present(texture)`：把纹理 `CopyResource` 到后缓冲后 `Present(1, 0)`
- 先用一张**纯色/测试渐变**纹理验证可见

- [ ] **Step 4: 接到 `IWallPaperModule::Start`，编译并人工验证**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat
```
启动应用（`Desktop Sticker/x64/Release/Desktop Sticker/Desktop_Sticker.exe`），**人工确认**：
1. 桌面背景出现测试图案，位于**桌面图标之下**（图标可见且可点击）
2. 分区卡片在图案**之上**
3. 双击桌面空白仍能切换干净桌面（壁纸窗口未截获双击）
4. 桌面图标可正常双击打开

失败排查顺序：先确认 `FindWallpaperWorkerW` 返回值非空；再确认 `SetParent` 后 `GetAncestor` 相符；再确认 DComp `Commit` 返回 `S_OK`。把每一步结果写进 `%APPDATA%\DesktopSticker\debug.log`（沿用既有 `[source]` 标记风格，用 `[wallpaper]`）。

- [ ] **Step 5: 验证降级路径**

临时让 `FindWallpaperWorkerW` 返回 `nullptr`（或注释掉 `SetParent`），确认窗口仍以置底普通窗口出现、不崩、分区与时钟正常。

- [ ] **Step 6: 跑测试确认无回归 + Commit**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && ./Tests/DesktopSticker.Tests.exe   # 期望仍为上一阶段数字
cd "D:/project/Desktop Sticker"
git add "Desktop Sticker/DesktopSticker.WallPaper"
git commit -m "feat(wallpaper): d3d11 composition surface embedded below desktop icons"
```

---

### Task 14: 专用线程 + 帧调度循环

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/FrameSchedulerLoop.h`、`.cpp`
- Modify: `WallPaperWindow.cpp/.h`、`WallPaperModule.cpp`、`DesktopSticker.WallPaper.vcxproj`

**Interfaces:**
- Consumes: `FrameScheduler`（策略）、`D3dContext`、`WallPaperWindow`
- Produces:
  - `FrameSchedulerLoop::Start(DesktopHost, D3dContext&, WallPaperWindow&)` / `Stop()`
  - 线程内：创建窗口 → 消息循环 + `QPC` 计时的呈现循环
  - `FrameSchedulerLoop::SetPaused(bool)` / `SetTextureProvider(std::function<bool(ID3D11Texture2D**)>)`

- [ ] **Step 1: 实现线程与消息循环**

要求：
- 专用 `std::thread`；窗口与 D3D/DComp **全部在该线程创建与销毁**（不在宿主 UI 线程）
- 用 `PeekMessage` 非阻塞泵 + `MsgWaitForMultipleObjects` 等待到下一帧期限；期限用 `next_deadline_not_before` 计算
- 暂停状态：`schedule_frame` 返回等待/跳积压；暂停时不取帧、不 Present
- 退出：投递 `WM_QUIT`（`PostThreadMessage`），join 线程，销毁窗口

- [ ] **Step 2: 纹理提供者抽象**

`SetTextureProvider` 让本任务先用"测试图案序列"（渐变 + 帧序号），供 Task 15/16 替换为真实解码帧。用 `ID3D11ShaderResourceView` 与一个简单的顶点/像素着色器做 NV12→RGB 与 BGRA 直通两条路径（先用 BGRA 直通）。

- [ ] **Step 3: 接到 `Start`/`Stop`，编译并人工验证**

启动后应看到测试图案**逐帧变化**且流畅；暂停开关能停住；CPU 占用合理。

- [ ] **Step 4: 退出清理验证**

托盘"退出" → 进程退出、**无游离窗口**（用 `tools/window_visibility.ps1` 确认无残留）、桌面图标正常还原。

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(wallpaper): dedicated render thread with qpc frame scheduling"
```

---

## Phase E — 解码

### Task 15: FfmpegApi 动态加载 + 软解出帧

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/FfmpegApi.h`、`.cpp`
- Modify: `WallPaperModule.cpp`、`FrameSchedulerLoop.cpp`、vcxproj

**Interfaces:**
- Consumes: Task 14 的纹理提供者接口
- Produces:
  - `FfmpegApi::Load(const std::wstring& ffmpegDir)` → `bool`；用 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` 加载 `avformat/avcodec/avutil/swscale`，`GetProcAddress` 解析所需符号
  - `FfmpegDecoder`：`Open(path)`、`NextFrame(...)`、`SeekToStart()`、`Duration()`
  - 失败语义：目录缺失或符号缺失 → `Load` 返回 false，**只禁用 FFmpeg 兜底**，不影响 MF 主路径

- [ ] **Step 1: 实现受限搜索的动态加载**

硬约束：**只从传入目录解析**，不从当前工作目录或 `PATH`；加载失败逐级记录缺哪个 DLL/符号到 `[wallpaper]` 日志。

- [ ] **Step 2: 实现软解 → `sws_scale` 到 BGRA → 上传 D3D 纹理**

用 `avformat_open_input`/`av_find_best_stream`/`avcodec_send_packet`/`avcodec_receive_frame` 标准流程；`sws_scale` 输出 `AV_PIX_FMT_BGRA`；`ID3D11DeviceContext::UpdateSubresource` 或 `Map` 上传。循环播放：EOF 时 `av_seek_frame(0)`。

- [ ] **Step 3: 人工验证**

用一段本地 H.264 mp4 作为壁纸，确认 FFmpeg 路径能持续循环播放、无明显丢帧；`tools/ffmpeg/` 改名后确认**自动降级**（MF 仍工作或报错但不崩）。

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(wallpaper): dynamic ffmpeg shared-library loading and software decode"
```

---

### Task 16: MediaFoundationDecoder 主路径 + 兜底切换

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/MediaFoundationDecoder.h`、`.cpp`
- Modify: `WallPaperModule.cpp`、`FrameSchedulerLoop.cpp`、vcxproj

**Interfaces:**
- Consumes: `D3dContext` 的 D3D11 设备、`FfmpegDecoder`
- Produces:
  - `MediaFoundationDecoder::Open(path, ID3D11Device*)` → `bool`；用 `MFCreateDXGIDeviceManager` 把设备交给 `IMFSourceReader`，请求 `MFVideoFormat_NV12` 输出
  - `bool SupportsCodec(path)` → 供主/兜底决策
  - 决策顺序：MF 打开成功 → 用 MF；MF 明确不支持该素材 → `FfmpegApi::Load` 已成功则用 FFmpeg；两者皆不可用 → 保持最后一帧并报状态

- [ ] **Step 1: 实现 MF Source Reader**

要求：`MFStartup`/`MFShutdown` 生命周期与线程一致；`MF_SOURCE_READER_D3D_MANAGER` + `MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING`；输出 `NV12` 的 DXGI 背衬 `IMFSample`；把 NV12 纹理经着色器转 RGB 呈现（Task 14 已备好的 NV12 路径在此启用）。

- [ ] **Step 2: 实现主/兜底决策并记日志**

决策结果（MF / FFmpeg / 不可用）写入 `[wallpaper]` 日志，供排查混合显卡环境。

- [ ] **Step 3: 人工验证两种路径**

- 普通 H.264 → 走 MF（看日志）
- 构造一个 MF 不支持的素材（如 AV1 或 HEVC 无系统解码器的样本）→ 日志显示回落 FFmpeg 并成功播放
- 把 `tools/ffmpeg/` 移除 + 用不支持素材 → 明确报"不可用"，不崩、不黑屏崩溃

- [ ] **Step 4: 跑测试 + Commit**

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release" && ./Tests/DesktopSticker.Tests.exe
cd "D:/project/Desktop Sticker"
git commit -m "feat(wallpaper): media foundation primary decode with ffmpeg fallback"
```

---

## Phase F — 转码与缩略图

### Task 17: FfmpegTranscoder 子进程 + 缩略图

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/FfmpegTranscoder.h`、`.cpp`
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/ThumbnailGenerator.h`、`.cpp`
- Modify: `MediaLibrary.cpp`、`WallPaperModule.cpp`、vcxproj

**Interfaces:**
- Consumes: `build_transcode_args`、`variant_file_name`（策略）、`MediaLibrary`
- Produces:
  - `FfmpegTranscoder::Enqueue(job)` —— 串行队列，一次一个进程
  - `FfmpegTranscoder::Run(input, output, kind, timeout)` → `bool`；`CreateProcessW`（**绝对路径**、`CREATE_NO_WINDOW`、显式命令行，不走 shell）+ 超时 `TerminateProcess`
  - `ThumbnailGenerator::Generate(videoPath, outPngPath)` —— 抽一帧存 `poster.png`（用 WIC 编码）

- [ ] **Step 1: 实现 `FfmpegTranscoder`**

硬约束：
- 命令行**走显式参数数组拼接并按 Windows 规则加引号**，不经过 shell（避免注入）
- `ffmpeg.exe` 用**绝对路径**（`<exeDir>\ffmpeg\ffmpeg.exe`）
- 超时（如 10 分钟）到点 `TerminateProcess` 并记日志
- 输出路径必须落在库内该条目的 `variants\` 下；执行前断言输出 != 源文件

- [ ] **Step 2: 实现 `ThumbnailGenerator`**

用 FFmpeg 抽帧（`-frames:v 1` 到临时 PNG）或 MF，再用 WIC 缩放存为 `poster.png`（尺寸受限，例如宽 ≤ 512）。

- [ ] **Step 3: 导入时排入后台任务**

`MediaLibrary::Import` 成功后：生成 poster + 排入 `balanced` 转码。任一失败只记日志并标记该副本不可用，**不影响导入本身**。

- [ ] **Step 4: 人工验证**

导入一个 mp4 → 等待后 `variants\balanced-v1.mp4` 与 `poster.png` 出现；**源文件 hash 与导入前一致**；把 `tools/ffmpeg/ffmpeg.exe` 移除后导入 → 明确提示副本生成不可用，导入仍成功。

- [ ] **Step 5: 跑测试 + Commit**

```bash
git commit -m "feat(wallpaper): ffmpeg transcode queue and poster thumbnail generation"
```

---

## Phase G — 集成与收尾

### Task 18: 暂停策略接线（全屏 / 锁屏 / 息屏 / 手动）

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallPaper/src/FullscreenDetector.h`、`.cpp`
- Modify: `FrameSchedulerLoop.cpp`、`WallPaperModule.cpp`、`IWallPaperModule.h`、vcxproj

**Interfaces:**
- Consumes: `reduce_pause_policy`（策略）、`SetPaused`
- Produces:
  - `FullscreenDetector::AnyFullscreenCovering(HWND selfExclude)` —— `EnumWindows` 枚举**所有可见、未最小化、且未被 DWM 隐藏**的顶层窗口（不只看焦点窗口），排除自身与桌面层；判定其矩形覆盖整个显示器工作区
  - `SessionState::Locked()`、`SessionState::DisplayOff()` —— `WTSGetActiveConsoleSessionId` + `GetForegroundWindow`/`OpenInputDesktop` 失败判定锁屏；`MonitorFromPoint` + `GetMonitorInfo` 判定熄屏
  - `IWallPaperModule` 增加 `SetUserPaused(bool)` 与 `bool IsUserPaused()`

- [ ] **Step 1: 实现 `FullscreenDetector`**

要求：忽略 `WS_EX_TOOLWINDOW`、`WS_EX_NOACTIVATE`、桌面层窗口与自身；重复位置事件合并；与既有分区/时钟"双击空白检测"的窗口排除规则保持一致（避免把壁纸窗口当成遮挡源）。

- [ ] **Step 2: 实现会话/显示器状态探测**

- [ ] **Step 3: 接线到 `FrameSchedulerLoop`**

组装 `PauseInputs` → `reduce_pause_policy` → `SetPaused`；状态变化时触发 `WallPaperEvents::playbackStateChanged`。轮询用低频兜底（如 1s）+ 窗口事件触发，避免忙等。

- [ ] **Step 4: 人工验证**

- 打开一个全屏游戏/全屏视频 → 壁纸暂停（GPU 占用下降）；`Alt+Tab` 切回 → 恢复
- `Win+L` 锁屏 → 停止；解锁 → 恢复
- 设置里手动暂停 → 暂停；取消 → 恢复
- 分区拖拽、时钟、干净桌面全程无回归

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(wallpaper): pause on fullscreen, session lock and display off"
```

---

### Task 19: 设置页"动态壁纸"卡片

**Files:**
- Modify: `Desktop Sticker/Desktop Sticker/SettingsController.h`、`.cpp`
- Modify: `Desktop Sticker/Desktop Sticker/Host.h`（暴露 `WallPaper()`，已在 Task 4 完成）
- Modify: `Desktop Sticker/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`（若新增纯逻辑，如列表排序/状态文案）

**Interfaces:**
- Consumes: `IWallPaperModule` 的全部方法、`Host::WallPaper()`
- Produces: 设置页卡片；新增可单测的纯函数（若有）放 `include/desktopsticker/wallpaper/`

- [ ] **Step 1: 在设置页新增卡片**

沿用现有 Win11 设置页卡片风格（Mica 之上叠圆角卡片、图标 + 标题 + 描述）。控件：
- 开关：启用动态壁纸
- 当前壁纸名 + 存储位置（只读回显 `libraryRoot`）
- 缩略图列表（横向）：点击切换当前壁纸
- 按钮：导入视频（`IFileOpenDialog`）、重命名、删除、更改存储位置、重新生成性能副本
- 档位选择：原画 / 均衡 / 省电
- 开关：全屏时暂停、锁屏时暂停
- 手动暂停/恢复

要求：
- **壁纸模块不可用**（`Host::WallPaper() == nullptr`）时，卡片显示"动态壁纸不可用"并禁用控件，**不得崩溃**——这是既有的次级窗口坑，所有交互回调都要能容忍空指针。
- 长耗时操作（导入、转码、生成缩略图）**不得阻塞 UI 线程**；缩略图异步加载并缓存。

- [ ] **Step 2: 订阅事件刷新**

`WallPaperEvents::libraryChanged` / `playbackStateChanged` → 刷新卡片。注意既有约定：窗口被 X 关闭后 XAML `Window` 对象已销毁，回调必须先判断窗口是否仍然有效。

- [ ] **Step 3: 人工验证**

- 打开设置页（托盘菜单 / `--settings`）→ 卡片可见
- 导入视频 → 缩略图出现、可切换、切换后桌面壁纸变化
- 重命名 / 删除 / 更改存储位置 / 重新生成副本 均生效
- 手动暂停与档位切换生效
- 把 `DesktopSticker.WallPaper.dll` 改名 → 设置页显示不可用且不崩；改回
- **验证既有设置页功能无回归**（搜索范围、热键、磁贴间距、时钟开关、恢复桌面）

- [ ] **Step 4: 跑测试 + Commit**

```bash
git commit -m "feat(wallpaper): wallpaper card in settings page"
```

---

### Task 20: 端到端验收与文档收尾

**Files:**
- Modify: `README.md`、`AGENTS.md`
- Modify: `docs/acceptance.md`（新增动态壁纸验收项）

**Interfaces:**
- Consumes: 全部前置任务
- Produces: 完整可交付功能 + 更新的文档

- [ ] **Step 1: 跑完整测试基线**

```bash
cd "D:/project/Desktop Sticker" && cmd //c build.bat test
```
Expected: 全部通过（38 + Phase B/C 新增），`0 failed`

- [ ] **Step 2: 逐条走规格第 11 节验收标准（9 条）**

每条记录实际观察结果。重点是：
1. 壁纸在图标与分区之下
2. MF 主路径 + FFmpeg 兜底都能播（看日志确认实际走了哪条）
3. 副本可生成/删除/重建且源文件字节不变（比对 hash）
4. 三项暂停均生效
5. 库与设置落在预期位置，损坏不崩
6. 缺负载 / 缺 DLL 时其余功能完全正常
7. 测试全绿
8. 许可文件齐备、FFmpeg 为动态加载
9. 退出后无游离窗口、图标正常还原

- [ ] **Step 3: 性能观测**

观测 CPU、GPU Video Decode、工作集、句柄数；1080p 与 4K H.264/HEVC 各跑一段。把结果记入 `docs/acceptance.md` 备注。

- [ ] **Step 4: 更新文档**

- `README.md`：功能一览新增"动态桌面壁纸"；项目结构加入 `DesktopSticker.WallPaper/`；配置与数据表加入 `wallpaper.json` 与壁纸库位置；使用说明表格加入壁纸操作行
- `AGENTS.md`：布局加入新工程与 `tools/prepare_ffmpeg.ps1`；构建说明加入负载脚本；"Fragile areas"加入壁纸窗口的 WorkerW 嵌入与层级、DComp 子窗口、暂停检测；架构边界加入 `IWallPaperModule` 与 EXE 只消费该头文件的约定；约定加入"策略提炼成 header-only 纯函数以便 dtest 单测"

- [ ] **Step 5: 最终 Commit**

```bash
git add README.md AGENTS.md docs/acceptance.md
git commit -m "docs: document dynamic wallpaper feature, payload script and acceptance items"
```

---

## 自审记录

**规格覆盖检查**（规格章节 → 对应任务）：

| 规格章节 | 任务 |
|---|---|
| 3. 组件与边界（含 3.3 降级） | Task 3、Task 4 |
| 4.1 三来源一呈现路径（MF/FFmpeg/副本） | Task 13、14、15、16 |
| 4.2 档位选择策略 | Task 7、Task 19 |
| 5. 桌面嵌入与窗口层级 | Task 13（含 WorkerW 发现、HWND_BOTTOM、排除双击检测、降级） |
| 6.1 配置与库分离 | Task 10、Task 11 |
| 6.2 存储位置自动选择 | Task 5、Task 12 |
| 6.3 启动校验与降级 | Task 12 |
| 6.4 媒体操作（中等范围） | Task 11 |
| 7.1 暂停优先级 | Task 6、Task 18 |
| 7.2 线程模型 | Task 14 |
| 8.1–8.4 三方组件、锁定版本、运行期加载、声明落点 | Task 1、Task 2、Task 15 |
| 9.1 构建 | Task 3 |
| 9.2 单元测试 | Task 5–12（每任务含测试步骤） |
| 9.3 人工验收 | Task 13、14、16、17、18、19、20 |
| 10. 风险与降级 | Task 13 降级步骤、Task 15/16 缺失负载验证、Task 4 模块缺失降级 |
| 11. 验收标准 | Task 20 Step 2 |
| 12. 未纳入范围 | 无任务（正确，属排除项） |

**占位符检查**：无 TBD/TODO；每个代码步骤都给了可编译的实义代码或明确的 API 序列与不变量。

**类型一致性检查**：
- `VariantKind` 定义在 `Types.h`（Task 3），被 Task 7/9/10/11/17 使用，名称一致。
- `DriveInfo`/`DriveKind` 定义在 `StoragePlacement.h`（Task 5），被 Task 12 复用，名称一致。
- `PauseInputs`/`PauseDecision`/`reduce_pause_policy`（Task 6）与 Task 18 的使用一致。
- `schedule_frame`/`next_deadline_not_before`（Task 8）与 Task 14 使用一致。
- `build_transcode_args`/`variant_file_name`（Task 9）与 Task 17 使用一致。
- `IWallPaperModule` 方法名在 Task 3 定义，Task 4/18/19 引用一致（Task 18 新增 `SetUserPaused`/`IsUserPaused`，已在 Task 18 接口块声明）。

**已知与规格的偏离（已确认合理）**：
- 规格 4.1 提到 "D3D11 着色器 NV12→RGB"：Task 14 先做 BGRA 直通以隔离呈现路径风险，Task 16 再启用 NV12 着色器路径。属于实现顺序安排，不改变设计。
- 规格 8.2 的负载版本号（`8.1`）与 zip 内 ffmpeg 版本号（`n8.1.2`）为同一构建的两种表述，Task 1/2 已按原文照抄，避免改写。
