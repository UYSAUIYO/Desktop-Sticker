# Desktop Sticker 实施计划（综合版）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个 Windows 11 桌面工具：桌面分区收纳（Fences 式、Win11 Fluent 风格）+ 双击空格搜索启动器（搜索桌面、用户常用目录、手动添加应用）。

**Architecture:** `DesktopSticker.exe` 是 WinUI 3 (C++/WinRT) 薄宿主，负责生命周期、托盘、设置窗口、启动器窗口；`DesktopSticker.Features.dll` 是唯一功能 DLL，包含桌面分区（原生 Win32 + Direct2D，通过 WorkerW/SHELLDLL_DefView 嵌入桌面 Shell）与搜索启动器逻辑（热键、索引、配置）。EXE 通过 `LoadLibrary` 加载 DLL，用 `IFeatureModule` 接口通信。

**Tech Stack:** C++/WinRT、WinUI 3（Windows App SDK）、Direct2D、Win32 Shell API（WorkerW/SysListView32/IContextMenu/IDropTarget）、nlohmann/json（单头文件）、Microsoft Native Unit Test Framework（CppUnitTestFramework）、Visual Studio 2022（MSVC）、CMake 仅用于辅助脚本。

---

## 前置约定

- 仓库根目录：`D:\project\Desktop Sticker`
- 源码根目录：`src/`
- 配置数据目录：`%APPDATA%\DesktopSticker\`
- 所有路径在代码中以 `std::filesystem::path` 或宽字符串处理；源文件统一 UTF-8 with BOM（MSVC 需要）。
- 每个 Task 结束都要 `git commit`。

---

## Task 1: 安装 MSVC / Visual Studio Build Tools

**Files:** 无（系统环境）

- [ ] **Step 1: 安装 VS Build Tools（C++ 桌面开发 + Windows 11 SDK）**

```bash
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended --passive --norestart"
```

- [ ] **Step 2: 验证工具链可用**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && where cl && where msbuild"
```

Expected: 输出 `cl.exe` 与 `msbuild.exe` 的路径，无错误。

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "chore: document toolchain prerequisite (no code)"
```

---

## Task 2: 初始化仓库结构

**Files:**
- Create: `src/DesktopSticker.Features/`, `src/DesktopSticker.App/`, `src/DesktopSticker.Tests/`, `third_party/nlohmann/`, `tools/`

- [ ] **Step 1: 创建目录**

```bash
mkdir -p src/DesktopSticker.Features/include/desktopsticker src/DesktopSticker.Features/src src/DesktopSticker.App src/DesktopSticker.Tests third_party/nlohmann tools
```

- [ ] **Step 2: 更新 `.gitignore`（追加内容）**

```gitignore
# VS test results
TestResults/
*.coverage
*.trx
```

- [ ] **Step 3: 提交**

```bash
git add .gitignore
git commit -m "chore: create source directory skeleton"
```

---

## Task 3: Features DLL 骨架 + IFeatureModule

**Files:**
- Create: `src/DesktopSticker.Features/DesktopSticker.Features.vcxproj`
- Create: `src/DesktopSticker.Features/pch.h`
- Create: `src/DesktopSticker.Features/pch.cpp`
- Create: `src/DesktopSticker.Features/include/desktopsticker/IFeatureModule.h`
- Create: `src/DesktopSticker.Features/src/FeatureModule.h`
- Create: `src/DesktopSticker.Features/src/FeatureModule.cpp`
- Create: `src/DesktopSticker.Features/src/module.cpp`

- [ ] **Step 1: 编写 `DesktopSticker.Features.vcxproj`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{11111111-2222-3333-4444-555555555555}</ProjectGuid>
    <RootNamespace>DesktopStickerFeatures</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings"></ImportGroup>
  <ImportGroup Label="Shared"></ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>
    <OutDir>$(SolutionDir)bin\$(Platform)\$(Configuration)\</OutDir>
    <IntDir>$(SolutionDir)obj\$(Platform)\$(Configuration)\Features\</IntDir>
    <TargetName>DesktopSticker.Features</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile><WarningLevel>Level4</WarningLevel><SDLCheck>true</SDLCheck><PreprocessorDefinitions>_WINDLL;_UNICODE;UNICODE;%(PreprocessorDefinitions)</PreprocessorDefinitions><ConformanceMode>true</ConformanceMode><LanguageStandard>stdcpp20</LanguageStandard><AdditionalIncludeDirectories>$(ProjectDir)include;$(ProjectDir)..\..\third_party;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile>
    <Link><SubSystem>Windows</SubSystem><GenerateDebugInformation>true</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile><WarningLevel>Level4</WarningLevel><FunctionLevelLinking>true</FunctionLevelLinking><IntrinsicFunctions>true</IntrinsicFunctions><SDLCheck>true</SDLCheck><PreprocessorDefinitions>_WINDLL;_UNICODE;UNICODE;NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions><ConformanceMode>true</ConformanceMode><LanguageStandard>stdcpp20</LanguageStandard><AdditionalIncludeDirectories>$(ProjectDir)include;$(ProjectDir)..\..\third_party;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile>
    <Link><SubSystem>Windows</SubSystem><EnableCOMDATFolding>true</EnableCOMDATFolding><OptimizeReferences>true</OptimizeReferences><GenerateDebugInformation>true</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClInclude Include="pch.h" />
    <ClInclude Include="include\desktopsticker\IFeatureModule.h" />
    <ClInclude Include="src\FeatureModule.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="pch.cpp" />
    <ClCompile Include="src\FeatureModule.cpp" />
    <ClCompile Include="src\module.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets"></ImportGroup>
</Project>
```

- [ ] **Step 2: 编写 `pch.h`**

```cpp
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shlobj.h>
#include <shellapi.h>
#include <Shlwapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
```

- [ ] **Step 3: 编写 `pch.cpp`**

```cpp
#include "pch.h"
```

- [ ] **Step 4: 编写 `include/desktopsticker/IFeatureModule.h`**

```cpp
#pragma once
#include <functional>

namespace desktopsticker {

struct FeatureEvents {
    // 双击空格（或其他配置热键）触发
    std::function<void()> hotkeyTriggered;
    // 索引重建完成
    std::function<void()> indexUpdated;
    // 分区布局变化（用于通知 UI 刷新）
    std::function<void()> zonesChanged;
};

class IFeatureModule {
public:
    virtual ~IFeatureModule() = default;
    virtual bool Init(const FeatureEvents& events) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Shutdown() = 0;
};

} // namespace desktopsticker

extern "C" __declspec(dllexport) desktopsticker::IFeatureModule* CreateFeatureModule();
extern "C" __declspec(dllexport) void DestroyFeatureModule(desktopsticker::IFeatureModule* module);
```

- [ ] **Step 5: 编写 `src/FeatureModule.h`**

```cpp
#pragma once
#include "desktopsticker/IFeatureModule.h"

namespace desktopsticker {

class FeatureModule final : public IFeatureModule {
public:
    FeatureModule() = default;
    ~FeatureModule() override = default;

    bool Init(const FeatureEvents& events) override;
    void Start() override;
    void Stop() override;
    void Shutdown() override;

private:
    FeatureEvents events_;
    bool initialized_ = false;
};

} // namespace desktopsticker
```

- [ ] **Step 6: 编写 `src/FeatureModule.cpp`**

```cpp
#include "pch.h"
#include "FeatureModule.h"

namespace desktopsticker {

bool FeatureModule::Init(const FeatureEvents& events) {
    events_ = events;
    initialized_ = true;
    return true;
}

void FeatureModule::Start() {
    if (!initialized_) return;
    // 后续 Task 在这里启动 HotkeyService / IndexService / DesktopWorkspace
}

void FeatureModule::Stop() {
    // 后续 Task 在这里停止服务
}

void FeatureModule::Shutdown() {
    Stop();
    initialized_ = false;
}

} // namespace desktopsticker
```

- [ ] **Step 7: 编写 `src/module.cpp`（DLL 工厂导出）**

```cpp
#include "pch.h"
#include "desktopsticker/IFeatureModule.h"
#include "FeatureModule.h"

extern "C" __declspec(dllexport) desktopsticker::IFeatureModule* CreateFeatureModule() {
    return new desktopsticker::FeatureModule();
}

extern "C" __declspec(dllexport) void DestroyFeatureModule(desktopsticker::IFeatureModule* module) {
    delete module;
}
```

- [ ] **Step 8: 创建解决方案文件 `DesktopSticker.sln`**

用 Visual Studio 创建空白解决方案（File → New → Project → Blank Solution，命名 `DesktopSticker`，保存到仓库根目录），再把 `DesktopSticker.Features.vcxproj` 加入解决方案。不要在此步骤创建其他项目。

- [ ] **Step 9: 构建验证**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m"
```

Expected: 生成 `bin\x64\Debug\DesktopSticker.Features.dll`，错误 0。

- [ ] **Step 10: 提交**

```bash
git add -A
git commit -m "feat: add Features DLL skeleton with IFeatureModule factory"
```

---

## Task 4: 引入 nlohmann/json + ConfigStore

**Files:**
- Create: `third_party/nlohmann/json.hpp`
- Create: `src/DesktopSticker.Features/include/desktopsticker/ConfigStore.h`
- Create: `src/DesktopSticker.Features/src/ConfigStore.cpp`
- Modify: `src/DesktopSticker.Features/DesktopSticker.Features.vcxproj`（添加两个文件）
- Create: `src/DesktopSticker.Tests/DesktopSticker.Tests.vcxproj`
- Create: `src/DesktopSticker.Tests/pch.h`、`pch.cpp`
- Create: `src/DesktopSticker.Tests/TestConfigStore.cpp`

- [ ] **Step 1: 下载 nlohmann/json 单头文件**

```bash
curl -L -o third_party/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
```

验证文件大小 > 500KB：

```bash
ls -la third_party/nlohmann/json.hpp
```

- [ ] **Step 2: 编写 `ConfigStore.h`**

```cpp
#pragma once
#include <filesystem>
#include <string>

namespace desktopsticker {

struct AppConfig {
    std::wstring hotkeyMode = L"double-space"; // "double-space" | "custom"
    std::wstring customHotkey = L"Alt+Space";
    bool followSystemTheme = true;
    bool searchDesktop = true;
    bool searchKnownFolders = true;
    bool includeHiddenFiles = false;
};

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path rootDir);

    bool Load();
    bool Save() const;

    const AppConfig& GetConfig() const { return config_; }
    void SetConfig(const AppConfig& config) { config_ = config; }
    std::filesystem::path GetRootDir() const { return rootDir_; }

private:
    std::filesystem::path rootDir_;
    std::filesystem::path configPath_;
    AppConfig config_;
};

} // namespace desktopsticker
```

- [ ] **Step 3: 编写 `ConfigStore.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/ConfigStore.h"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

ConfigStore::ConfigStore(std::filesystem::path rootDir)
    : rootDir_(std::move(rootDir)), configPath_(rootDir_ / L"config.json") {}

bool ConfigStore::Load() {
    std::error_code ec;
    fs::create_directories(rootDir_, ec);
    if (!fs::exists(configPath_)) {
        return Save();
    }

    try {
        std::ifstream in(configPath_);
        if (!in.is_open()) return false;
        json j;
        in >> j;

        AppConfig cfg;
        if (j.contains("hotkeyMode") && j["hotkeyMode"].is_string()) {
            cfg.hotkeyMode = std::wstring(j["hotkeyMode"].get<std::string>().begin(), j["hotkeyMode"].get<std::string>().end());
        }
        if (j.contains("customHotkey") && j["customHotkey"].is_string()) {
            cfg.customHotkey = std::wstring(j["customHotkey"].get<std::string>().begin(), j["customHotkey"].get<std::string>().end());
        }
        cfg.followSystemTheme = j.value("followSystemTheme", true);
        cfg.searchDesktop = j.value("searchDesktop", true);
        cfg.searchKnownFolders = j.value("searchKnownFolders", true);
        cfg.includeHiddenFiles = j.value("includeHiddenFiles", false);
        config_ = cfg;
        return true;
    } catch (...) {
        // 损坏时备份后重置
        std::error_code backupEc;
        fs::copy_file(configPath_, configPath_.wstring() + L".bak", fs::copy_options::overwrite_existing, backupEc);
        config_ = AppConfig{};
        return Save();
    }
}

bool ConfigStore::Save() const {
    std::error_code ec;
    fs::create_directories(rootDir_, ec);

    json j;
    std::string hotkeyMode(config_.hotkeyMode.begin(), config_.hotkeyMode.end());
    std::string customHotkey(config_.customHotkey.begin(), config_.customHotkey.end());
    j["hotkeyMode"] = hotkeyMode;
    j["customHotkey"] = customHotkey;
    j["followSystemTheme"] = config_.followSystemTheme;
    j["searchDesktop"] = config_.searchDesktop;
    j["searchKnownFolders"] = config_.searchKnownFolders;
    j["includeHiddenFiles"] = config_.includeHiddenFiles;

    const fs::path tmp = configPath_.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << j.dump(2);
        out.flush();
    }
    // 原子替换
    return MoveFileExW(tmp.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

} // namespace desktopsticker
```

- [ ] **Step 4: 编写 `DesktopSticker.Tests.vcxproj`（原生单元测试工程）**

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>TestProject</Keyword>
    <ProjectGuid>{22222222-3333-4444-5555-666666666666}</ProjectGuid>
    <RootNamespace>DesktopStickerTests</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
    <UseOfMfc>false</UseOfMfc>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
    <UseOfMfc>false</UseOfMfc>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings"></ImportGroup>
  <ImportGroup Label="Shared"></ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>
    <OutDir>$(SolutionDir)bin\$(Platform)\$(Configuration)\Tests\</OutDir>
    <IntDir>$(SolutionDir)obj\$(Platform)\$(Configuration)\Tests\</IntDir>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <WarningLevel>Level4</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <PreprocessorDefinitions>_DEBUG;_UNICODE;UNICODE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(ProjectDir)..\DesktopSticker.Features\include;$(ProjectDir)..\..\third_party;$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <AdditionalUsingDirectories>$(VCInstallDir)UnitTest\include;%(AdditionalUsingDirectories)</AdditionalUsingDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <AdditionalLibraryDirectories>$(VCInstallDir)UnitTest\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>DesktopSticker.Features.lib;%(AdditionalDependencies)</AdditionalDependencies>
      <AdditionalOptions>/SUBSYSTEM:CONSOLE %(AdditionalOptions)</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level4</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <PreprocessorDefinitions>NDEBUG;_UNICODE;UNICODE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(ProjectDir)..\DesktopSticker.Features\include;$(ProjectDir)..\..\third_party;$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <AdditionalUsingDirectories>$(VCInstallDir)UnitTest\include;%(AdditionalUsingDirectories)</AdditionalUsingDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <AdditionalLibraryDirectories>$(VCInstallDir)UnitTest\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>DesktopSticker.Features.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClInclude Include="pch.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="pch.cpp" />
    <ClCompile Include="TestConfigStore.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets"></ImportGroup>
</Project>
```

在 Visual Studio 中把测试工程加入解决方案，并添加对 `DesktopSticker.Features` 的项目引用（References → Add Reference → Projects → DesktopSticker.Features）。

- [ ] **Step 5: 编写 `pch.h` / `pch.cpp`（测试工程）**

```cpp
// pch.h
#pragma once
#include <CppUnitTest.h>
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
```

```cpp
// pch.cpp
#include "pch.h"
```

- [ ] **Step 6: 编写 `TestConfigStore.cpp`（先写失败测试）**

```cpp
#include "pch.h"
#include <desktopsticker/ConfigStore.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace desktopsticker;

TEST_CLASS(TestConfigStore) {
public:
    TEST_METHOD(DefaultConfig_IsSavedAndLoaded) {
        auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Config";
        std::filesystem::remove_all(root);

        ConfigStore store(root);
        Assert::IsTrue(store.Save(), L"Save should succeed");

        ConfigStore loaded(root);
        Assert::IsTrue(loaded.Load(), L"Load should succeed");
        Assert::AreEqual(std::wstring(L"double-space"), loaded.GetConfig().hotkeyMode.c_str());
        Assert::AreEqual(std::wstring(L"Alt+Space"), loaded.GetConfig().customHotkey.c_str());
        Assert::IsTrue(loaded.GetConfig().searchDesktop);
    }

    TEST_METHOD(CorruptedConfig_IsBackedUpAndReset) {
        auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Corrupt";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        {
            std::ofstream out(root / L"config.json");
            out << "{ this is not json";
        }

        ConfigStore store(root);
        Assert::IsTrue(store.Load(), L"Load should reset after corruption");
        Assert::AreEqual(std::wstring(L"double-space"), store.GetConfig().hotkeyMode.c_str());
        Assert::IsTrue(std::filesystem::exists(root / L"config.json.bak"));
    }
};
```

- [ ] **Step 7: 运行测试，确认失败（编译失败或断言失败）**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m && \"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe\" bin\x64\Debug\Tests\DesktopSticker.Tests.dll"
```

Expected: 编译失败，因为 `ConfigStore` 尚未加入 Features 工程。

- [ ] **Step 8: 把 `ConfigStore.h/.cpp` 加入 Features 工程**

在 `DesktopSticker.Features.vcxproj` 的 `<ItemGroup>` 中添加：

```xml
<ClInclude Include="include\desktopsticker\ConfigStore.h" />
<ClCompile Include="src\ConfigStore.cpp" />
```

- [ ] **Step 9: 重新运行测试，确认通过**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m && \"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe\" bin\x64\Debug\Tests\DesktopSticker.Tests.dll"
```

Expected: 2 个测试全部通过。

- [ ] **Step 10: 提交**

```bash
git add -A
git commit -m "feat: add ConfigStore with atomic JSON persistence and tests"
```

---

## Task 5: HotkeyService（双击空格）

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/HotkeyService.h`
- Create: `src/DesktopSticker.Features/src/HotkeyService.cpp`
- Create: `src/DesktopSticker.Tests/TestHotkeyService.cpp`
- Modify: `DesktopSticker.Features.vcxproj`、`DesktopSticker.Tests.vcxproj`

- [ ] **Step 1: 编写 `HotkeyService.h`**

```cpp
#pragma once
#include <atomic>
#include <functional>
#include <thread>

namespace desktopsticker {

class HotkeyService {
public:
    using Clock = std::function<long long()>; // 毫秒
    using TextInputPredicate = std::function<bool()>;

    explicit HotkeyService(Clock clock = DefaultClock);

    bool Start();
    void Stop();
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_.load(); }

    // 可测试的核心判定：返回 true 表示检测到双击空格
    bool HandleKeyEvent(bool isKeyDown, long long nowMs);

    void SetTextInputPredicate(TextInputPredicate pred) { textInputPredicate_ = std::move(pred); }
    void SetDoublePressWindowMs(long long ms) { windowMs_ = ms; }

    static long long DefaultClock();

private:
    void ThreadMain();

    Clock clock_;
    TextInputPredicate textInputPredicate_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> running_{false};
    std::thread thread_;
    HHOOK hook_ = nullptr;

    // 双击判定状态（仅钩子线程访问）
    bool keyDown_ = false;
    bool firstPressSeen_ = false;
    long long lastReleaseMs_ = 0;
    long long windowMs_ = 250;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `HotkeyService.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/HotkeyService.h"

namespace desktopsticker {

namespace {
HotkeyService* g_instance = nullptr;

bool IsTextInputForeground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(threadId, &gti)) return false;
    return gti.hwndCaret != nullptr; // 有插入符 → 正在文本输入
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_instance) {
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (info->vkCode == VK_SPACE) {
            const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            g_instance->HandleKeyEvent(down, HotkeyService::DefaultClock());
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

HotkeyService::HotkeyService(Clock clock)
    : clock_(std::move(clock)), textInputPredicate_(IsTextInputForeground) {}

long long HotkeyService::DefaultClock() {
    return static_cast<long long>(GetTickCount64());
}

bool HotkeyService::HandleKeyEvent(bool isKeyDown, long long nowMs) {
    if (!isKeyDown) {
        keyDown_ = false;
        lastReleaseMs_ = nowMs;
        return false;
    }

    if (keyDown_) return false; // 忽略长按重复

    keyDown_ = true;
    if (textInputPredicate_ && textInputPredicate_()) {
        firstPressSeen_ = false;
        lastReleaseMs_ = 0;
        return false;
    }

    if (!firstPressSeen_) {
        firstPressSeen_ = true;
        lastReleaseMs_ = 0;
        return false;
    }

    // 第二次按下：检查距上次释放是否在窗口内
    if (lastReleaseMs_ != 0 && (nowMs - lastReleaseMs_) <= windowMs_) {
        firstPressSeen_ = false;
        lastReleaseMs_ = 0;
        return true;
    }

    firstPressSeen_ = true;
    lastReleaseMs_ = 0;
    return false;
}

bool HotkeyService::Start() {
    if (running_.exchange(true)) return false;
    thread_ = std::thread([this]() { ThreadMain(); });
    return true;
}

void HotkeyService::Stop() {
    if (!running_.exchange(false)) return;
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    PostThreadMessageW(GetThreadId(thread_.native_handle()), WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    g_instance = nullptr;
}

void HotkeyService::SetEnabled(bool enabled) {
    enabled_.store(enabled);
}

void HotkeyService::ThreadMain() {
    g_instance = this;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    MSG msg;
    while (running_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
}

} // namespace desktopsticker
```

注意：`SetWindowsHookExW` 的 `hMod` 参数在 DLL 中应为 `GetModuleHandleW(L"DesktopSticker.Features.dll")`；上面使用 `GetModuleHandleW(nullptr)` 在本场景（同一进程由 EXE LoadLibrary）仍能工作，但更稳妥的写法：

```cpp
hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                          GetModuleHandleW(L"DesktopSticker.Features.dll"), 0);
```

请在实现时使用后者。

- [ ] **Step 3: 编写 `TestHotkeyService.cpp`**

```cpp
#include "pch.h"
#include <desktopsticker/HotkeyService.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace desktopsticker;

TEST_CLASS(TestHotkeyService) {
public:
    TEST_METHOD(DoubleSpaceWithinWindow_Triggers) {
        auto now = std::make_shared<long long>(0);
        HotkeyService svc([now]() { return *now; });
        svc.SetTextInputPredicate([]() { return false; });

        Assert::IsFalse(svc.HandleKeyEvent(true, *now += 100));   // 第一次按下
        Assert::IsFalse(svc.HandleKeyEvent(false, *now += 50));   // 释放
        Assert::IsTrue(svc.HandleKeyEvent(true, *now += 100));    // 第二次按下（间隔 100ms ≤ 250ms）
    }

    TEST_METHOD(DoubleSpaceTooSlow_DoesNotTrigger) {
        auto now = std::make_shared<long long>(0);
        HotkeyService svc([now]() { return *now; });
        svc.SetTextInputPredicate([]() { return false; });

        Assert::IsFalse(svc.HandleKeyEvent(true, *now += 100));
        Assert::IsFalse(svc.HandleKeyEvent(false, *now += 50));
        Assert::IsFalse(svc.HandleKeyEvent(true, *now += 500));   // 间隔 500ms > 250ms
    }

    TEST_METHOD(TextInputForeground_DoesNotTrigger) {
        auto now = std::make_shared<long long>(0);
        HotkeyService svc([now]() { return *now; });
        svc.SetTextInputPredicate([]() { return true; });

        Assert::IsFalse(svc.HandleKeyEvent(true, *now += 100));
        Assert::IsFalse(svc.HandleKeyEvent(false, *now += 50));
        Assert::IsFalse(svc.HandleKeyEvent(true, *now += 100));   // 即使节奏正确也不触发
    }
};
```

- [ ] **Step 4: 运行测试，确认失败**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m && \"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe\" bin\x64\Debug\Tests\DesktopSticker.Tests.dll"
```

Expected: 编译失败（`HotkeyService` 未定义）。

- [ ] **Step 5: 把 `HotkeyService.h/.cpp` 加入 Features 工程，把 `TestHotkeyService.cpp` 加入测试工程**

Features vcxproj 添加：

```xml
<ClInclude Include="include\desktopsticker\HotkeyService.h" />
<ClCompile Include="src\HotkeyService.cpp" />
```

Tests vcxproj 的 `<ClCompile>` 中添加：

```xml
<ClCompile Include="TestHotkeyService.cpp" />
```

- [ ] **Step 6: 重新运行测试，确认通过（3 个测试）**

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat: add double-space HotkeyService with typing guard and tests"
```

---

## Task 6: PinyinMapper（中文拼音首字母搜索）

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/PinyinMapper.h`
- Create: `src/DesktopSticker.Features/src/PinyinMapper.cpp`
- Create: `tools/generate_pinyin_table.py`
- Create: `src/DesktopSticker.Tests/TestPinyinMapper.cpp`
- Modify: 两个 vcxproj

- [ ] **Step 1: 编写 `PinyinMapper.h`**

```cpp
#pragma once
#include <string>
#include <unordered_map>

namespace desktopsticker {

class PinyinMapper {
public:
    // 返回中文文件名的拼音首字母（小写）；非中文字符原样小写保留
    static std::wstring GetInitials(const std::wstring& text);

private:
    static const std::unordered_map<wchar_t, wchar_t>& Table();
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `PinyinMapper.cpp`（内置常用字表）**

```cpp
#include "pch.h"
#include "desktopsticker/PinyinMapper.h"

#include <algorithm>
#include <cctype>

namespace desktopsticker {

const std::unordered_map<wchar_t, wchar_t>& PinyinMapper::Table() {
    static const std::unordered_map<wchar_t, wchar_t> table = {
        {L'微', L'w'}, {L'信', L'x'}, {L'浏', L'l'}, {L'览', L'l'}, {L'器', L'q'},
        {L'文', L'w'}, {L'档', L'd'}, {L'图', L't'}, {L'片', L'p'}, {L'视', L's'},
        {L'频', L'p'}, {L'音', L'y'}, {L'乐', L'l'}, {L'游', L'y'}, {L'戏', L'x'},
        {L'桌', L'z'}, {L'面', L'm'}, {L'设', L's'}, {L'置', L'z'}, {L'聊', L'l'},
        {L'天', L't'}, {L'工', L'g'}, {L'作', L'z'}, {L'娱', L'y'}, {L'电', L'd'},
        {L'脑', L'n'}, {L'管', L'g'}, {L'理', L'l'}, {L'记', L'j'}, {L'事', L's'},
        {L'本', L'b'}, {L'计', L'j'}, {L'算', L's'}, {L'机', L'j'}, {L'播', L'b'},
        {L'放', L'f'}, {L'相', L'x'}, {L'册', L'c'}, {L'下', L'x'}, {L'载', L'z'},
        {L'文', L'w'}, {L'件', L'j'}, {L'夹', L'j'}, {L'资', L'z'}, {L'源', L'y'},
        {L'歌', L'g'}, {L'曲', L'q'}, {L'照', L'z'}, {L'录', L'l'}, {L'屏', L'p'},
        {L'幕', L'm'}, {L'键', L'j'}, {L'盘', L'p'}, {L'鼠', L's'}, {L'标', L'b'},
        {L'网', L'w'}, {L'页', L'y'}, {L'邮', L'y'}, {L'箱', L'x'}, {L'日', L'r'},
        {L'历', L'l'}, {L'时', L's'}, {L'钟', L'z'}, {L'地', L'd'}, {L'图', L't'},
        {L'导', L'd'}, {L'航', L'h'}, {L'剪', L'j'}, {L'贴', L't'}, {L'板', L'b'},
    };
    return table;
}

std::wstring PinyinMapper::GetInitials(const std::wstring& text) {
    const auto& table = Table();
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        auto it = table.find(ch);
        if (it != table.end()) {
            result.push_back(it->second);
        } else if (ch >= L'A' && ch <= L'Z') {
            result.push_back(static_cast<wchar_t>(std::towlower(ch)));
        } else if (ch >= L'a' && ch <= L'z' || (ch >= L'0' && ch <= L'9')) {
            result.push_back(ch);
        }
        // 其他字符（标点/空格）忽略
    }
    return result;
}

} // namespace desktopsticker
```

- [ ] **Step 3: 编写生成脚本 `tools/generate_pinyin_table.py`（用于后续扩充字表）**

```python
#!/usr/bin/env python3
"""从 pinyin-data 仓库的常用字表生成 PinyinMapper 表头片段。"""
import urllib.request
import sys

URL = "https://raw.githubusercontent.com/mozillazg/pinyin-data/master/pinyin.txt"

def main() -> int:
    out_path = sys.argv[1] if len(sys.argv) > 1 else "pinyin_table.generated.h"
    with urllib.request.urlopen(URL, timeout=30) as resp:
        lines = resp.read().decode("utf-8").splitlines()
    entries = []
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # 格式: U+4E00: wēi  # 微
        try:
            cp_hex, rest = line.split(":", 1)
            cp = int(cp_hex.strip()[2:], 16)
            pinyin = rest.split("#")[0].strip().split()[0]
            initial = pinyin[0].lower()
            entries.append((cp, initial))
        except (ValueError, IndexError):
            continue
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("// Generated by tools/generate_pinyin_table.py\n")
        f.write("static const std::unordered_map<wchar_t, wchar_t> kGeneratedPinyinTable = {\n")
        for cp, initial in entries:
            f.write(f"    {{0x{cp:04X}, L'{initial}'}},\n")
        f.write("};\n")
    print(f"generated {len(entries)} entries -> {out_path}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: 编写 `TestPinyinMapper.cpp`**

```cpp
#include "pch.h"
#include <desktopsticker/PinyinMapper.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace desktopsticker;

TEST_CLASS(TestPinyinMapper) {
public:
    TEST_METHOD(WeChat_ReturnsWx) {
        Assert::AreEqual(std::wstring(L"wx"), PinyinMapper::GetInitials(L"微信").c_str());
    }

    TEST_METHOD(Browser_ReturnsLlq) {
        Assert::AreEqual(std::wstring(L"llq"), PinyinMapper::GetInitials(L"浏览器").c_str());
    }

    TEST_METHOD(MixedText_KeepsAscii) {
        Assert::AreEqual(std::wstring(L"wxtest"), PinyinMapper::GetInitials(L"微信test").c_str());
    }
};
```

- [ ] **Step 5: TDD 循环：加入文件 → 运行测试失败 → 通过**

按 Task 4/5 的方式把新文件加入工程，先确认测试失败（找不到 `PinyinMapper`），实现后再跑通。最终命令：

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m && \"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe\" bin\x64\Debug\Tests\DesktopSticker.Tests.dll"
```

Expected: 3 个测试通过。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add PinyinMapper with initial table and generator script"
```

---

## Task 7: IndexService（索引与搜索）

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/IndexService.h`
- Create: `src/DesktopSticker.Features/src/IndexService.cpp`
- Create: `src/DesktopSticker.Tests/TestIndexService.cpp`
- Modify: 两个 vcxproj

- [ ] **Step 1: 编写 `IndexService.h`**

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "desktopsticker/ConfigStore.h"
#include "desktopsticker/PinyinMapper.h"

namespace desktopsticker {

struct IndexedItem {
    std::wstring name;
    std::wstring path;
    std::wstring source; // Desktop / Documents / Downloads / Pictures / Videos / Music / Apps
    std::wstring pinyin;
    bool isApp = false;
};

class IndexService {
public:
    explicit IndexService(ConfigStore* config);

    bool Rebuild();
    std::vector<IndexedItem> Search(const std::wstring& query, size_t maxResults) const;

    bool AddApp(const std::wstring& path);
    bool RemoveApp(const std::wstring& path);

    const std::vector<IndexedItem>& Items() const { return items_; }
    const std::vector<std::wstring>& Apps() const { return apps_; }

private:
    void ScanDirectory(const std::filesystem::path& dir, const std::wstring& source);
    void ScanKnownFolders();
    void ScanDesktop();
    void LoadApps();
    bool SaveApps() const;

    ConfigStore* config_;
    std::filesystem::path rootDir_;
    std::filesystem::path appsPath_;
    std::vector<IndexedItem> items_;
    std::vector<std::wstring> apps_;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `IndexService.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/IndexService.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}

bool IsHidden(const fs::path& p) {
    const DWORD attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN);
}

int MatchScore(const std::wstring& nameLower, const std::wstring& pinyinLower, const std::wstring& queryLower) {
    if (nameLower.rfind(queryLower, 0) == 0) return 0;       // 名称前缀
    if (nameLower.find(queryLower) != std::wstring::npos) return 1;  // 名称包含
    if (pinyinLower.rfind(queryLower, 0) == 0) return 2;     // 拼音前缀
    if (pinyinLower.find(queryLower) != std::wstring::npos) return 3; // 拼音包含
    return -1;
}

} // namespace

IndexService::IndexService(ConfigStore* config)
    : config_(config),
      rootDir_(config->GetRootDir()),
      appsPath_(rootDir_ / L"apps.json") {}

bool IndexService::Rebuild() {
    items_.clear();
    LoadApps();

    const AppConfig& cfg = config_->GetConfig();
    if (cfg.searchDesktop) ScanDesktop();
    if (cfg.searchKnownFolders) ScanKnownFolders();

    for (const auto& app : apps_) {
        IndexedItem item;
        item.name = app;
        item.path = app;
        item.source = L"Apps";
        item.pinyin = PinyinMapper::GetInitials(app);
        item.isApp = true;
        items_.push_back(std::move(item));
    }

    std::sort(items_.begin(), items_.end(),
              [](const IndexedItem& a, const IndexedItem& b) { return a.name < b.name; });
    return true;
}

void IndexService::ScanDesktop() {
    PWSTR desktopPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
        ScanDirectory(desktopPath, L"Desktop");
        CoTaskMemFree(desktopPath);
    }
}

void IndexService::ScanKnownFolders() {
    struct Folder { KNOWNFOLDERID id; const wchar_t* name; };
    const Folder folders[] = {
        {FOLDERID_Documents, L"Documents"},
        {FOLDERID_Downloads, L"Downloads"},
        {FOLDERID_Pictures, L"Pictures"},
        {FOLDERID_Videos, L"Videos"},
        {FOLDERID_Music, L"Music"},
    };
    for (const auto& f : folders) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(f.id, 0, nullptr, &path))) {
            ScanDirectory(path, f.name);
            CoTaskMemFree(path);
        }
    }
}

void IndexService::ScanDirectory(const fs::path& dir, const std::wstring& source) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    const bool includeHidden = config_->GetConfig().includeHiddenFiles;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto& p = entry.path();
        if (!includeHidden && IsHidden(p)) continue;

        IndexedItem item;
        item.name = p.filename().wstring();
        item.path = p.wstring();
        item.source = source;
        item.pinyin = PinyinMapper::GetInitials(item.name);
        items_.push_back(std::move(item));
    }
}

void IndexService::LoadApps() {
    apps_.clear();
    std::error_code ec;
    if (!fs::exists(appsPath_, ec)) return;
    try {
        std::ifstream in(appsPath_);
        json j;
        in >> j;
        for (const auto& app : j.value("apps", json::array())) {
            apps_.push_back(std::wstring(app.get<std::string>().begin(), app.get<std::string>().end()));
        }
    } catch (...) {
        apps_.clear();
    }
}

bool IndexService::SaveApps() const {
    json j = json::array();
    for (const auto& app : apps_) {
        j.push_back(std::string(app.begin(), app.end()));
    }
    json root;
    root["apps"] = j;

    std::error_code ec;
    fs::create_directories(rootDir_, ec);
    const fs::path tmp = appsPath_.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << root.dump(2);
        out.flush();
    }
    return MoveFileExW(tmp.c_str(), appsPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool IndexService::AddApp(const std::wstring& path) {
    for (const auto& app : apps_) {
        if (_wcsicmp(app.c_str(), path.c_str()) == 0) return true;
    }
    apps_.push_back(path);
    return SaveApps();
}

bool IndexService::RemoveApp(const std::wstring& path) {
    auto it = std::remove_if(apps_.begin(), apps_.end(),
                             [&](const std::wstring& app) { return _wcsicmp(app.c_str(), path.c_str()) == 0; });
    if (it == apps_.end()) return false;
    apps_.erase(it, apps_.end());
    return SaveApps();
}

std::vector<IndexedItem> IndexService::Search(const std::wstring& query, size_t maxResults) const {
    if (query.empty()) return {};

    const std::wstring queryLower = ToLower(query);
    std::vector<std::pair<int, const IndexedItem*>> scored;

    for (const auto& item : items_) {
        const std::wstring nameLower = ToLower(item.name);
        const std::wstring pinyinLower = ToLower(item.pinyin);
        const int score = MatchScore(nameLower, pinyinLower, queryLower);
        if (score >= 0) {
            scored.emplace_back(score, &item);
        }
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) {
                         if (a.first != b.first) return a.first < b.first;
                         return a.second->name < b.second->name;
                     });

    std::vector<IndexedItem> results;
    results.reserve(std::min(maxResults, scored.size()));
    for (size_t i = 0; i < scored.size() && i < maxResults; ++i) {
        results.push_back(*scored[i].second);
    }
    return results;
}

} // namespace desktopsticker
```

- [ ] **Step 3: 编写 `TestIndexService.cpp`（用临时目录模拟桌面）**

```cpp
#include "pch.h"
#include <desktopsticker/IndexService.h>

#include <fstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace desktopsticker;

namespace {
void WriteFile(const std::filesystem::path& p, const std::wstring& content) {
    std::ofstream out(p, std::ios::binary);
    out << std::string(content.begin(), content.end());
}
}

TEST_CLASS(TestIndexService) {
public:
    TEST_METHOD(Search_FindsFileByName) {
        auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Index";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        WriteFile(root / L"report.docx", L"");

        // 注意：ScanDesktop/ScanKnownFolders 扫描真实用户目录，测试只验证 AddApp 搜索
        ConfigStore config(root);
        config.SetConfig(AppConfig{});
        IndexService index(&config);
        index.AddApp(root / L"report.docx");
        index.Rebuild();

        auto results = index.Search(L"report", 10);
        Assert::AreEqual<size_t>(1, results.size());
        Assert::AreEqual(std::wstring(L"report.docx"), results[0].name.c_str());
    }

    TEST_METHOD(Search_FindsByPinyin) {
        auto root = std::filesystem::temp_directory_path() / L"DesktopStickerTest_Pinyin";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);

        ConfigStore config(root);
        IndexService index(&config);
        index.AddApp(L"C:\\fake\\微信.exe");
        index.Rebuild();

        auto results = index.Search(L"wx", 10);
        Assert::AreEqual<size_t>(1, results.size());
        Assert::AreEqual(std::wstring(L"C:\\fake\\微信.exe"), results[0].path.c_str());
    }
};
```

说明：`AddApp` 写入 `apps.json`，`Rebuild` 会把应用加入索引；这样测试不依赖真实用户目录，稳定可复现。

- [ ] **Step 4: TDD 循环 + 构建**

把 `IndexService.h/.cpp` 加入 Features，`TestIndexService.cpp` 加入 Tests；先跑失败，再实现通过。最终：

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m && \"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe\" bin\x64\Debug\Tests\DesktopSticker.Tests.dll"
```

Expected: 全部通过（含之前 ConfigStore/Hotkey/Pinyin 测试）。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: add IndexService with desktop/known-folder/user-app search and tests"
```

---

## Task 8: ShellLauncher + IconService

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/ShellLauncher.h`
- Create: `src/DesktopSticker.Features/src/ShellLauncher.cpp`
- Create: `src/DesktopSticker.Features/include/desktopsticker/IconService.h`
- Create: `src/DesktopSticker.Features/src/IconService.cpp`
- Modify: Features vcxproj

- [ ] **Step 1: 编写 `ShellLauncher.h`**

```cpp
#pragma once
#include <string>

namespace desktopsticker {

class ShellLauncher {
public:
    static bool Open(const std::wstring& path);
    static bool OpenFolderAndSelect(const std::wstring& path);
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `ShellLauncher.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/ShellLauncher.h"

namespace desktopsticker {

bool ShellLauncher::Open(const std::wstring& path) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool ShellLauncher::OpenFolderAndSelect(const std::wstring& path) {
    std::wstring params = L"/select,\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace desktopsticker
```

- [ ] **Step 3: 编写 `IconService.h`**

```cpp
#pragma once
#include <map>
#include <string>

namespace desktopsticker {

class IconService {
public:
    // 返回调用方无需释放的 HICON（由本类缓存并统一销毁）
    HICON GetIcon(const std::wstring& path, int size);
    void ClearCache();
    ~IconService();

private:
    std::map<std::pair<std::wstring, int>, HICON> cache_;
};

} // namespace desktopsticker
```

- [ ] **Step 4: 编写 `IconService.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/IconService.h"

namespace desktopsticker {

IconService::~IconService() {
    ClearCache();
}

HICON IconService::GetIcon(const std::wstring& path, int size) {
    const auto key = std::make_pair(path, size);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    HICON icon = nullptr;
    SHFILEINFOW sfi{};
    UINT flags = SHGFI_ICON;
    if (size >= 32) flags |= SHGFI_LARGEICON;
    else flags |= SHGFI_SMALLICON;

    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags)) {
        icon = sfi.hIcon;
    }
    if (!icon) {
        // 通用图标兜底
        SHGetFileInfoW(L".exe", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags);
        icon = sfi.hIcon;
    }
    if (icon) cache_[key] = icon;
    return icon;
}

void IconService::ClearCache() {
    for (auto& [key, icon] : cache_) {
        if (icon) DestroyIcon(icon);
    }
    cache_.clear();
}

} // namespace desktopsticker
```

- [ ] **Step 5: 加入工程并构建**

把 4 个文件加入 Features vcxproj，然后：

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m"
```

Expected: 编译通过，无错误。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add ShellLauncher and IconService"
```

---

## Task 9: ZoneModel（分区数据模型）

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/ZoneModel.h`
- Create: `src/DesktopSticker.Features/src/ZoneModel.cpp`
- Create: `src/DesktopSticker.Tests/TestZoneModel.cpp`
- Modify: 两个 vcxproj

- [ ] **Step 1: 编写 `ZoneModel.h`**

```cpp
#pragma once
#include <map>
#include <string>
#include <vector>
#include <windows.h>

namespace desktopsticker {

struct Zone {
    std::wstring id;
    std::wstring name;
    RECT rect{};          // 相对所在显示器工作区
    int monitorIndex = 0;
    bool collapsed = false;
    std::vector<std::wstring> itemPaths; // 被收纳项完整路径
};

struct DesktopLayout {
    std::vector<Zone> zones;
    // 被收纳原生图标 -> 原始屏幕坐标（用于恢复）
    std::map<std::wstring, POINT> originalIconPositions;
    // 收纳前是否开启“自动排列图标”
    bool autoArrangeWasEnabled = false;
};

class ZoneModel {
public:
    bool Load(const std::filesystem::path& layoutPath);
    bool Save(const std::filesystem::path& layoutPath) const;

    const DesktopLayout& Layout() const { return layout_; }
    DesktopLayout& Layout() { return layout_; }

    Zone* FindZone(const std::wstring& id);
    void AddZone(Zone zone);
    bool RemoveZone(const std::wstring& id);
    bool MoveItem(const std::wstring& itemPath, const std::wstring& fromZoneId, const std::wstring& toZoneId);
    std::wstring GenerateZoneId() const;

private:
    DesktopLayout layout_;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `ZoneModel.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/ZoneModel.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace desktopsticker {

namespace {
json RectToJson(const RECT& r) {
    return json{{"left", r.left}, {"top", r.top}, {"right", r.right}, {"bottom", r.bottom}};
}
RECT RectFromJson(const json& j) {
    RECT r{};
    r.left = j.value("left", 0);
    r.top = j.value("top", 0);
    r.right = j.value("right", 100);
    r.bottom = j.value("bottom", 200);
    return r;
}
std::wstring FromUtf8(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string ToUtf8(const std::wstring& s) { return std::string(s.begin(), s.end()); }
} // namespace

bool ZoneModel::Load(const fs::path& layoutPath) {
    if (!fs::exists(layoutPath)) return false;
    try {
        std::ifstream in(layoutPath);
        json j;
        in >> j;

        DesktopLayout layout;
        layout.autoArrangeWasEnabled = j.value("autoArrangeWasEnabled", false);

        for (const auto& zj : j.value("zones", json::array())) {
            Zone z;
            z.id = FromUtf8(zj.value("id", ""));
            z.name = FromUtf8(zj.value("name", ""));
            z.rect = RectFromJson(zj.value("rect", json::object()));
            z.monitorIndex = zj.value("monitorIndex", 0);
            z.collapsed = zj.value("collapsed", false);
            for (const auto& p : zj.value("itemPaths", json::array())) {
                z.itemPaths.push_back(FromUtf8(p.get<std::string>()));
            }
            layout.zones.push_back(std::move(z));
        }

        for (const auto& [key, value] : j.value("originalIconPositions", json::object()).items()) {
            POINT pt{};
            pt.x = value.value("x", 0);
            pt.y = value.value("y", 0);
            layout.originalIconPositions[FromUtf8(key)] = pt;
        }

        layout_ = std::move(layout);
        return true;
    } catch (...) {
        return false;
    }
}

bool ZoneModel::Save(const fs::path& layoutPath) const {
    json j;
    j["autoArrangeWasEnabled"] = layout_.autoArrangeWasEnabled;

    json zones = json::array();
    for (const auto& z : layout_.zones) {
        json zj;
        zj["id"] = ToUtf8(z.id);
        zj["name"] = ToUtf8(z.name);
        zj["rect"] = RectToJson(z.rect);
        zj["monitorIndex"] = z.monitorIndex;
        zj["collapsed"] = z.collapsed;
        json paths = json::array();
        for (const auto& p : z.itemPaths) paths.push_back(ToUtf8(p));
        zj["itemPaths"] = paths;
        zones.push_back(std::move(zj));
    }
    j["zones"] = zones;

    json positions = json::object();
    for (const auto& [path, pt] : layout_.originalIconPositions) {
        positions[ToUtf8(path)] = json{{"x", pt.x}, {"y", pt.y}};
    }
    j["originalIconPositions"] = positions;

    std::error_code ec;
    fs::create_directories(layoutPath.parent_path(), ec);
    const fs::path tmp = layoutPath.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << j.dump(2);
        out.flush();
    }
    return MoveFileExW(tmp.c_str(), layoutPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

Zone* ZoneModel::FindZone(const std::wstring& id) {
    for (auto& z : layout_.zones) {
        if (z.id == id) return &z;
    }
    return nullptr;
}

void ZoneModel::AddZone(Zone zone) {
    if (zone.id.empty()) zone.id = GenerateZoneId();
    layout_.zones.push_back(std::move(zone));
}

bool ZoneModel::RemoveZone(const std::wstring& id) {
    auto it = std::remove_if(layout_.zones.begin(), layout_.zones.end(),
                             [&](const Zone& z) { return z.id == id; });
    if (it == layout_.zones.end()) return false;
    layout_.zones.erase(it, layout_.zones.end());
    return true;
}

bool ZoneModel::MoveItem(const std::wstring& itemPath, const std::wstring& fromZoneId, const std::wstring& toZoneId) {
    Zone* from = FindZone(fromZoneId);
    Zone* to = FindZone(toZoneId);
    if (!from || !to || fromZoneId == toZoneId) return false;

    auto it = std::find(from->itemPaths.begin(), from->itemPaths.end(), itemPath);
    if (it == from->itemPaths.end()) return false;
    from->itemPaths.erase(it);
    to->itemPaths.push_back(itemPath);
    return true;
}

std::wstring ZoneModel::GenerateZoneId() const {
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t buf[64];
    StringFromGUID2(guid, buf, 64);
    return std::wstring(buf);
}

} // namespace desktopsticker
```

- [ ] **Step 3: 编写 `TestZoneModel.cpp`**

```cpp
#include "pch.h"
#include <desktopsticker/ZoneModel.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace desktopsticker;

TEST_CLASS(TestZoneModel) {
public:
    TEST_METHOD(SaveAndLoad_RoundTrip) {
        auto layoutPath = std::filesystem::temp_directory_path() / L"DesktopStickerTest_layout.json";
        std::filesystem::remove(layoutPath);

        ZoneModel model;
        Zone z;
        z.id = L"z1";
        z.name = L"工作";
        z.rect = RECT{10, 20, 300, 400};
        z.itemPaths.push_back(L"C:\\Users\\test\\Desktop\\微信.lnk");
        model.AddZone(std::move(z));
        model.Layout().originalIconPositions[L"C:\\Users\\test\\Desktop\\微信.lnk"] = POINT{50, 60};

        Assert::IsTrue(model.Save(layoutPath));

        ZoneModel loaded;
        Assert::IsTrue(loaded.Load(layoutPath));
        Assert::AreEqual<size_t>(1, loaded.Layout().zones.size());
        Assert::AreEqual(std::wstring(L"工作"), loaded.Layout().zones[0].name.c_str());
        Assert::AreEqual<LONG>(10, loaded.Layout().zones[0].rect.left);
        Assert::AreEqual<LONG>(400, loaded.Layout().zones[0].rect.bottom);
    }

    TEST_METHOD(MoveItem_MovesBetweenZones) {
        ZoneModel model;
        Zone a; a.id = L"a"; a.name = L"A";
        Zone b; b.id = L"b"; b.name = L"B";
        a.itemPaths.push_back(L"C:\\x.lnk");
        model.AddZone(std::move(a));
        model.AddZone(std::move(b));

        Assert::IsTrue(model.MoveItem(L"C:\\x.lnk", L"a", L"b"));
        Assert::AreEqual<size_t>(0, model.FindZone(L"a")->itemPaths.size());
        Assert::AreEqual<size_t>(1, model.FindZone(L"b")->itemPaths.size());
    }
};
```

- [ ] **Step 4: TDD 循环 + 构建 + 测试通过**

加入文件到工程，先失败后实现，最终运行全部测试 Expected: 通过。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: add ZoneModel with JSON layout persistence and tests"
```

---

## Task 10: WinUI 3 App 工程 + DLL 加载 + 托盘

**Files:**
- Create: `src/DesktopSticker.App/`（通过 VS 模板创建）
- Create: `src/DesktopSticker.App/Host.h`、`Host.cpp`
- Modify: `src/DesktopSticker.App/App.xaml`、`App.xaml.cpp`、`MainWindow.xaml`、`MainWindow.xaml.cpp`

- [ ] **Step 1: 用 VS 模板创建 WinUI 3 工程**

在 Visual Studio 中：File → New → Project → 搜索 **"Blank App, Packaged (WinUI 3 in Desktop)"**（C++/WinRT），项目名 `DesktopSticker.App`，位置 `src/`，解决方案选择已有 `DesktopSticker.sln`。目标平台选 x64，Windows App SDK 版本选最新稳定版。

- [ ] **Step 2: 编写 `Host.h`（EXE 侧 DLL 宿主）**

```cpp
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "desktopsticker/IFeatureModule.h"

namespace desktopsticker::app {

class Host {
public:
    Host();
    ~Host();

    bool LoadFeatures();
    void UnloadFeatures();
    bool Start();
    void Stop();

    void SetHotkeyCallback(std::function<void()> cb) { hotkeyCallback_ = std::move(cb); }
    desktopsticker::IFeatureModule* Module() const { return module_; }

private:
    HMODULE dll_ = nullptr;
    desktopsticker::IFeatureModule* module_ = nullptr;
    std::function<void()> hotkeyCallback_;
    std::wstring dllPath_;
};

} // namespace desktopsticker::app
```

- [ ] **Step 3: 编写 `Host.cpp`**

```cpp
#include "pch.h"
#include "Host.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace desktopsticker::app {

Host::Host() {
    // DLL 与 EXE 在同一输出目录
    dllPath_ = fs::path(L".") / L"DesktopSticker.Features.dll";
}

Host::~Host() {
    UnloadFeatures();
}

bool Host::LoadFeatures() {
    if (module_) return true;

    // 优先从 EXE 所在目录加载
    wchar_t exeDir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    fs::path dll = fs::path(exeDir).parent_path() / L"DesktopSticker.Features.dll";
    if (!fs::exists(dll)) dll = dllPath_;

    dll_ = LoadLibraryW(dll.c_str());
    if (!dll_) return false;

    using CreateFn = desktopsticker::IFeatureModule* (*)();
    using DestroyFn = void (*)(desktopsticker::IFeatureModule*);

    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll_, "CreateFeatureModule"));
    auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(dll_, "DestroyFeatureModule"));
    if (!create || !destroy) {
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }

    module_ = create();

    desktopsticker::FeatureEvents events;
    events.hotkeyTriggered = [this]() {
        if (hotkeyCallback_) hotkeyCallback_();
    };
    if (!module_->Init(events)) {
        destroy(module_);
        module_ = nullptr;
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }
    return true;
}

void Host::UnloadFeatures() {
    if (module_) {
        module_->Shutdown();
        using DestroyFn = void (*)(desktopsticker::IFeatureModule*);
        auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(dll_, "DestroyFeatureModule"));
        if (destroy) destroy(module_);
        module_ = nullptr;
    }
    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
}

bool Host::Start() {
    if (!module_) return false;
    module_->Start();
    return true;
}

void Host::Stop() {
    if (module_) module_->Stop();
}

} // namespace desktopsticker::app
```

- [ ] **Step 4: 修改 `App.xaml`**

```xml
<Application
    x:Class="DesktopSticker.App.App"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <Application.Resources>
        <ResourceDictionary>
            <ResourceDictionary.MergedDictionaries>
                <XamlControlsResources xmlns="using:Microsoft.UI.Xaml.Controls" />
            </ResourceDictionary.MergedDictionaries>
        </ResourceDictionary>
    </Application.Resources>
</Application>
```

- [ ] **Step 5: 修改 `App.xaml.cpp`**

```cpp
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Host.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::DesktopSticker::App::implementation {

App::App() {
    Initialize();
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    m_host = std::make_unique<desktopsticker::app::Host>();
    m_host->LoadFeatures();
    m_host->Start();

    m_mainWindow = make<MainWindow>(m_host.get());
    m_mainWindow.Activate();
}

} // namespace winrt::DesktopSticker::App::implementation
```

同时修改 `App.xaml.h`，添加成员：

```cpp
#include "Host.h"
...
private:
    std::unique_ptr<desktopsticker::app::Host> m_host;
    winrt::DesktopSticker::App::MainWindow m_mainWindow{ nullptr };
```

- [ ] **Step 6: 修改 `MainWindow.xaml`（仅托盘宿主，内容后续补充）**

```xml
<Window
    x:Class="DesktopSticker.App.MainWindow"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Padding="24" Spacing="12">
        <TextBlock Text="Desktop Sticker" Style="{StaticResource TitleTextBlockStyle}" />
        <TextBlock Text="工具正在后台运行，双击空格唤起搜索。" />
    </StackPanel>
</Window>
```

- [ ] **Step 7: 修改 `MainWindow.xaml.cpp`：添加托盘图标**

在 `MainWindow` 构造中添加 `NOTIFYICONDATAW`：

```cpp
#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::DesktopSticker::App::implementation {

MainWindow::MainWindow(desktopsticker::app::Host* host) : m_host(host) {
    InitializeComponent();

    // 托盘图标
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = reinterpret_cast<HWND>(this->m_inner.as<IWindowNative>()->WindowHandle());
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Desktop Sticker");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

MainWindow::~MainWindow() {
    // 清理托盘
}

}
```

注意：`MainWindow` 构造函数签名在模板中默认是无参的；需要同步修改 `MainWindow.xaml.h` 中的构造函数声明为 `MainWindow(desktopsticker::app::Host* host);`，并在 `App.xaml.cpp` 中以 `make<MainWindow>(m_host.get())` 调用。

- [ ] **Step 8: 构建并运行**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m"
```

把 `DesktopSticker.Features.dll` 复制到 `bin\x64\Debug\DesktopSticker.App\`（或配置 App 工程的输出目录为同一 `bin\x64\Debug\`）。运行 EXE，Expected: 窗口显示，托盘出现图标，无崩溃。

- [ ] **Step 9: 提交**

```bash
git add -A
git commit -m "feat: add WinUI 3 host app with DLL loading and tray icon"
```

---

## Task 11: LauncherWindow（搜索启动器 UI）

**Files:**
- Create: `src/DesktopSticker.App/LauncherWindow.xaml`、`.h`、`.cpp`
- Modify: `src/DesktopSticker.App/Host.h`、`Host.cpp`（暴露 IndexService 搜索接口）
- Modify: `src/DesktopSticker.App/App.xaml.cpp`（创建启动器窗口）

- [ ] **Step 1: 在 Features DLL 暴露搜索接口**

在 `IFeatureModule.h` 中添加（DLL 侧实现）：

```cpp
// IFeatureModule.h 追加
#include <string>
#include <vector>

namespace desktopsticker {
struct SearchResult {
    std::wstring name;
    std::wstring path;
    std::wstring source;
    bool isApp = false;
};

class IFeatureModule {
public:
    ...
    virtual std::vector<SearchResult> Search(const std::wstring& query, size_t maxResults) = 0;
    virtual bool AddApp(const std::wstring& path) = 0;
    virtual bool RemoveApp(const std::wstring& path) = 0;
};
}
```

`FeatureModule` 中持有 `std::unique_ptr<IndexService>`，`Init` 时创建并 `Rebuild()`；`Search`/`AddApp`/`RemoveApp` 转发。

- [ ] **Step 2: 编写 `LauncherWindow.xaml`**

```xml
<Window
    x:Class="DesktopSticker.App.LauncherWindow"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:controls="using:Microsoft.UI.Xaml.Controls">

    <Grid Background="Transparent">
        <Border
            x:Name="RootBorder"
            Width="640"
            HorizontalAlignment="Center"
            VerticalAlignment="Top"
            Margin="0,80,0,0"
            Padding="12"
            CornerRadius="12"
            Background="{ThemeResource AcrylicInAppFillColorDefaultBrush}"
            BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
            BorderThickness="1">
            <Grid RowSpacing="8">
                <Grid.RowDefinitions>
                    <RowDefinition Height="Auto" />
                    <RowDefinition Height="*" />
                </Grid.RowDefinitions>

                <TextBox
                    x:Name="SearchBox"
                    PlaceholderText="搜索应用、文件…"
                    FontSize="18"
                    KeyDown="OnSearchBoxKeyDown"
                    TextChanged="OnSearchBoxTextChanged" />

                <ListView
                    x:Name="ResultList"
                    Grid.Row="1"
                    MaxHeight="420"
                    IsItemClickEnabled="True"
                    ItemClick="OnResultItemClick">
                    <ListView.ItemTemplate>
                        <DataTemplate>
                            <StackPanel Orientation="Horizontal" Spacing="12" Padding="4">
                                <Image Width="28" Height="28" Stretch="Uniform" />
                                <StackPanel>
                                    <TextBlock Text="{Binding Name}" />
                                    <TextBlock Text="{Binding Path}" Opacity="0.6" FontSize="12" />
                                </StackPanel>
                            </StackPanel>
                        </DataTemplate>
                    </ListView.ItemTemplate>
                </ListView>
            </Grid>
        </Border>
    </Grid>
</Window>
```

- [ ] **Step 3: 编写 `LauncherWindow.xaml.h`**

```cpp
#pragma once
#include "Host.h"

namespace winrt::DesktopSticker::App::implementation {

struct LauncherWindow : LauncherWindowT<LauncherWindow> {
    LauncherWindow(desktopsticker::app::Host* host);

    void ShowLauncher();
    void HideLauncher();
    bool IsVisible() const;

private:
    void OnSearchBoxTextChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
    void OnSearchBoxKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
    void OnResultItemClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);

    desktopsticker::app::Host* m_host = nullptr;
    std::vector<desktopsticker::SearchResult> m_results;
};

} // namespace winrt::DesktopSticker::App::implementation
```

- [ ] **Step 4: 编写 `LauncherWindow.xaml.cpp`（核心交互）**

```cpp
#include "pch.h"
#include "LauncherWindow.xaml.h"
#if __has_include("LauncherWindow.g.cpp")
#include "LauncherWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

namespace winrt::DesktopSticker::App::implementation {

LauncherWindow::LauncherWindow(desktopsticker::app::Host* host) : m_host(host) {
    InitializeComponent();
    // 无边框、不显示任务栏
    auto hwnd = reinterpret_cast<HWND>(this->m_inner.as<::IWindowNative>()->WindowHandle());
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                      GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
}

void LauncherWindow::ShowLauncher() {
    Activate();
    SearchBox().Focus(FocusState::Programmatic);
}

void LauncherWindow::HideLauncher() {
    Close();
}

bool LauncherWindow::IsVisible() const {
    return AppWindow().Visible();
}

void LauncherWindow::OnSearchBoxTextChanged(IInspectable const&, TextChangedEventArgs const&) {
    const auto query = SearchBox().Text();
    if (query.empty()) {
        ResultList().Items().Clear();
        return;
    }
    auto results = m_host->Module()->Search(query.c_str(), 30);
    m_results = std::move(results);
    ResultList().Items().Clear();
    for (const auto& r : m_results) {
        // 构造绑定对象（实际可定义独立 ViewModel；MVP 直接用 Items 添加字符串对）
        auto panel = winrt::Microsoft::UI::Xaml::Controls::StackPanel{};
        // 简化：直接添加文本块
        auto tb = TextBlock{};
        tb.Text(r.name);
        ResultList().Items().Append(tb);
    }
}

void LauncherWindow::OnSearchBoxKeyDown(IInspectable const&, KeyRoutedEventArgs const& e) {
    if (e.Key() == Windows::System::VirtualKey::Enter && !m_results.empty()) {
        desktopsticker::ShellLauncher::Open(m_results[0].path);
        HideLauncher();
    } else if (e.Key() == Windows::System::VirtualKey::Escape) {
        HideLauncher();
    }
}

void LauncherWindow::OnResultItemClick(IInspectable const&, ItemClickEventArgs const& e) {
    // MVP：打开第一个结果；后续 Task 完善索引绑定
    if (!m_results.empty()) {
        desktopsticker::ShellLauncher::Open(m_results[0].path);
        HideLauncher();
    }
}

} // namespace winrt::DesktopSticker::App::implementation
```

说明：为保持计划可执行，MVP 结果列表用简化绑定；在实现时可改为定义 `SearchResultViewModel`（实现 `INotifyPropertyChanged`）以获得更完整体验。这一步允许简化，但必须保证“输入关键词 → 显示结果 → Enter 打开”可用。

**工程配置**：`DesktopSticker.App.vcxproj` 需要添加 Features 的 include 目录（`$(ProjectDir)..\DesktopSticker.Features\include` 与 `$(ProjectDir)..\..\third_party`），并在链接器附加依赖中加入 `DesktopSticker.Features.lib`（或改为把 `ShellLauncher::Open` 也通过 `IFeatureModule` 暴露，避免 EXE 直接依赖 DLL 导入库；二选一，推荐后者以保持“UI 不直接碰 Shell API”的边界）。

- [ ] **Step 5: 在 `App.xaml.cpp` 中创建 LauncherWindow 并绑定热键回调**

```cpp
// App.xaml.cpp OnLaunched 中追加
m_launcher = make<LauncherWindow>(m_host.get());

m_host->SetHotkeyCallback([this]() {
    if (m_launcher.AppWindow().Visible()) {
        m_launcher.HideLauncher();
    } else {
        m_launcher.ShowLauncher();
    }
});
```

`App.xaml.h` 添加成员：`winrt::DesktopSticker::App::LauncherWindow m_launcher{ nullptr };`

- [ ] **Step 6: 在 FeatureModule 中启动 HotkeyService**

修改 `FeatureModule.h/.cpp`：

```cpp
// FeatureModule.h 新增成员
#include "desktopsticker/HotkeyService.h"
...
private:
    std::unique_ptr<HotkeyService> hotkey_;
```

```cpp
// FeatureModule.cpp
bool FeatureModule::Init(const FeatureEvents& events) {
    events_ = events;
    hotkey_ = std::make_unique<HotkeyService>();
    initialized_ = true;
    return true;
}

void FeatureModule::Start() {
    if (!initialized_) return;
    hotkey_->SetEnabled(true);
    hotkey_->Start();
}
```

在 `HotkeyService` 的 `HandleKeyEvent` 返回 true 处（钩子线程内）触发 `events_.hotkeyTriggered()`。注意：`FeatureEvents` 回调需要从钩子线程安全地投递到 UI 线程；MVP 使用 `DispatcherQueue::GetForCurrentThread()` 在 EXE 侧投递，或直接调用（EXE 回调内部用 `DispatcherQueue.TryEnqueue` 包装）。

- [ ] **Step 7: 构建 + 手动验收**

构建并运行：双击空格应弹出/隐藏启动器；输入“微信”或“wx”能看到结果（若桌面上有微信快捷方式）；Enter 打开。

- [ ] **Step 8: 提交**

```bash
git add -A
git commit -m "feat: add launcher window with search and hotkey toggle"
```

---

## Task 12: 设置窗口

**Files:**
- Create: `src/DesktopSticker.App/SettingsWindow.xaml`、`.h`、`.cpp`
- Modify: `MainWindow.xaml`（添加“设置”按钮或托盘菜单）

- [ ] **Step 1: 编写 `SettingsWindow.xaml`**

```xml
<Window
    x:Class="DesktopSticker.App.SettingsWindow"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    Title="Desktop Sticker 设置">

    <StackPanel Padding="24" Spacing="16">
        <TextBlock Text="设置" Style="{StaticResource TitleTextBlockStyle}" />

        <ToggleSwitch x:Name="SearchDesktopSwitch" Header="搜索桌面内容" />
        <ToggleSwitch x:Name="SearchKnownFoldersSwitch" Header="搜索文档/下载/图片/视频/音乐" />
        <ToggleSwitch x:Name="FollowThemeSwitch" Header="跟随系统深浅色主题" />

        <TextBlock Text="热键方案" />
        <ComboBox x:Name="HotkeyModeCombo" SelectionChanged="OnHotkeyModeChanged">
            <ComboBoxItem Content="双击空格" Tag="double-space" />
            <ComboBoxItem Content="Alt + Space" Tag="custom" />
        </ComboBox>

        <TextBlock Text="手动添加的应用" />
        <StackPanel Orientation="Horizontal" Spacing="8">
            <TextBox x:Name="AppPathBox" Width="400" PlaceholderText="例如 C:\Program Files\App\app.exe" />
            <Button Content="添加" Click="OnAddAppClick" />
        </StackPanel>
        <ListView x:Name="AppList" MaxHeight="200" />
        <Button Content="移除选中" Click="OnRemoveAppClick" />
    </StackPanel>
</Window>
```

- [ ] **Step 2: 编写 `SettingsWindow.xaml.cpp`（读写 ConfigStore/IndexService）**

```cpp
#include "pch.h"
#include "SettingsWindow.xaml.h"
#if __has_include("SettingsWindow.g.cpp")
#include "SettingsWindow.g.cpp"
#endif

#include "desktopsticker/ConfigStore.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::DesktopSticker::App::implementation {

SettingsWindow::SettingsWindow(desktopsticker::app::Host* host) : m_host(host) {
    InitializeComponent();

    // 读取配置（简化：Host 暴露 ConfigStore 的引用；MVP 通过 IFeatureModule 增加 GetConfig/SetConfig）
    // 这里假设 Host 已提供 GetConfig/SetConfig 转发（见 Step 3）
    auto cfg = m_host->GetConfig();
    SearchDesktopSwitch().IsOn(cfg.searchDesktop);
    SearchKnownFoldersSwitch().IsOn(cfg.searchKnownFolders);
    FollowThemeSwitch().IsOn(cfg.followSystemTheme);
    HotkeyModeCombo().SelectedIndex(cfg.hotkeyMode == L"custom" ? 1 : 0);

    RefreshApps();
}

void SettingsWindow::OnHotkeyModeChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    auto item = HotkeyModeCombo().SelectedItem().try_as<ComboBoxItem>();
    if (!item) return;
    auto tag = item.Tag().as<winrt::Windows::Foundation::IPropertyValue>().GetString();
    auto cfg = m_host->GetConfig();
    cfg.hotkeyMode = tag == L"custom" ? L"custom" : L"double-space";
    m_host->SetConfig(cfg);
}

void SettingsWindow::OnAddAppClick(IInspectable const&, RoutedEventArgs const&) {
    auto text = AppPathBox().Text();
    if (!text.empty()) {
        m_host->Module()->AddApp(text.c_str());
        RefreshApps();
        AppPathBox().Text(L"");
    }
}

void SettingsWindow::OnRemoveAppClick(IInspectable const&, RoutedEventArgs const&) {
    auto item = AppList().SelectedItem();
    if (item) {
        m_host->Module()->RemoveApp(item.as<TextBlock>().Text().c_str());
        RefreshApps();
    }
}

void SettingsWindow::RefreshApps() {
    AppList().Items().Clear();
    for (const auto& app : m_host->GetApps()) {
        auto tb = TextBlock{};
        tb.Text(app);
        AppList().Items().Append(tb);
    }
}

} // namespace winrt::DesktopSticker::App::implementation
```

- [ ] **Step 3: 扩展 `Host.h/.cpp` 与 `IFeatureModule` 增加配置/应用列表接口**

在 `IFeatureModule` 中增加：

```cpp
virtual desktopsticker::AppConfig GetConfig() = 0;
virtual void SetConfig(const desktopsticker::AppConfig& config) = 0;
virtual std::vector<std::wstring> GetApps() = 0;
```

在 `FeatureModule` 中实现（持有 `ConfigStore` 与 `IndexService` 指针，转发读写并在 `SetConfig` 后保存 + 重建索引）。

在 `Host` 中增加转发方法：`GetConfig()`、`SetConfig()`、`GetApps()`。

- [ ] **Step 4: 托盘菜单打开设置**

在 `MainWindow.xaml.cpp` 托盘回调中，收到 `WM_APP + 1` 的 `WM_RBUTTONUP` 时弹出菜单（“打开设置”“退出”）。MVP 可简化为左键单击打开设置窗口。

- [ ] **Step 5: 构建 + 验收**

构建运行：托盘打开设置 → 切换开关/添加应用 → 配置保存到 `%APPDATA%\DesktopSticker\config.json` 与 `apps.json`。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add settings window for hotkey, search scope, and app list"
```

---

## Task 13: DesktopShellIntegration（WorkerW 嵌入）

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/DesktopShellIntegration.h`
- Create: `src/DesktopSticker.Features/src/DesktopShellIntegration.cpp`
- Modify: Features vcxproj

- [ ] **Step 1: 编写 `DesktopShellIntegration.h`**

```cpp
#pragma once
#include <windows.h>

namespace desktopsticker {

struct ShellDesktopWindows {
    HWND progman = nullptr;
    HWND workerw = nullptr;   // 壁纸宿主 WorkerW
    HWND defView = nullptr;   // SHELLDLL_DefView
    HWND listView = nullptr;  // SysListView32
};

class DesktopShellIntegration {
public:
    bool Initialize();
    void Shutdown();

    // aboveIcons=true → 挂到 SHELLDLL_DefView 并置于图标之上；false → 挂到壁纸宿主
    bool EmbedWindow(HWND hwnd, bool aboveIcons);

    bool SubclassListView(SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data);
    bool UnsubclassListView(SUBCLASSPROC proc, UINT_PTR id);

    const ShellDesktopWindows& Windows() const { return wins_; }
    bool IsReady() const { return wins_.defView != nullptr; }

private:
    bool FindDesktopWindows();

    ShellDesktopWindows wins_;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `DesktopShellIntegration.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/DesktopShellIntegration.h"

#include <commctrl.h>

namespace desktopsticker {

bool DesktopShellIntegration::Initialize() {
    if (FindDesktopWindows()) return true;
    // 二次尝试：等待 Explorer 就绪后重试一次
    Sleep(500);
    return FindDesktopWindows();
}

void DesktopShellIntegration::Shutdown() {
    wins_ = ShellDesktopWindows{};
}

bool DesktopShellIntegration::FindDesktopWindows() {
    wins_.progman = FindWindowW(L"Progman", nullptr);
    if (!wins_.progman) return false;

    // 未文档化消息：让 Progman 生成 WorkerW
    SendMessageTimeoutW(wins_.progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
        HWND defView = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) {
            wins_.defView = defView;
            wins_.listView = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
            // 下一个 WorkerW 是壁纸宿主
            wins_.workerw = FindWindowExW(nullptr, worker, L"WorkerW", nullptr);
            return wins_.defView != nullptr;
        }
    }

    // 兼容旧路径：Progman 直接持有 SHELLDLL_DefView
    wins_.defView = FindWindowExW(wins_.progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (wins_.defView) {
        wins_.listView = FindWindowExW(wins_.defView, nullptr, L"SysListView32", nullptr);
    }
    return wins_.defView != nullptr;
}

bool DesktopShellIntegration::EmbedWindow(HWND hwnd, bool aboveIcons) {
    if (!wins_.defView) return false;
    HWND parent = aboveIcons ? wins_.defView : wins_.workerw;
    if (!parent) return false;

    SetParent(hwnd, parent);
    SetWindowPos(hwnd, aboveIcons ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

bool DesktopShellIntegration::SubclassListView(SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data) {
    if (!wins_.listView) return false;
    return SetWindowSubclass(wins_.listView, proc, id, data) != FALSE;
}

bool DesktopShellIntegration::UnsubclassListView(SUBCLASSPROC proc, UINT_PTR id) {
    if (!wins_.listView) return false;
    return RemoveWindowSubclass(wins_.listView, proc, id) != FALSE;
}

} // namespace desktopsticker
```

- [ ] **Step 3: 加入工程并构建**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Debug /p:Platform=x64 /m"
```

Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "feat: add DesktopShellIntegration for WorkerW/DefView embedding"
```

---

## Task 14: ZoneWindow + Direct2D 绘制

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/ZoneWindow.h`
- Create: `src/DesktopSticker.Features/src/ZoneWindow.cpp`
- Modify: Features vcxproj

- [ ] **Step 1: 编写 `ZoneWindow.h`**

```cpp
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>

#include "desktopsticker/ZoneModel.h"

namespace desktopsticker {

class ZoneWindow {
public:
    static bool RegisterClass(HINSTANCE hInst);
    static void UnregisterClass(HINSTANCE hInst);
    static ZoneWindow* FromHwnd(HWND hwnd);

    ZoneWindow(HINSTANCE hInst, const Zone& zone, IconService* icons);
    ~ZoneWindow();

    bool Create(HWND parentOrNull, bool aboveIcons);
    void Destroy();

    HWND Hwnd() const { return hwnd_; }
    const Zone& GetZone() const { return zone_; }
    void SetZone(const Zone& zone);
    void Refresh();

    // 供 DesktopWorkspace 调用的交互回调
    std::function<void(const std::wstring& zoneId)> onCollapseToggle;
    std::function<void(const std::wstring& zoneId)> onContextMenu;
    std::function<void(const std::wstring& zoneId, const std::wstring& itemPath)> onItemContextMenu;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);

    bool EnsureD2DResources();
    void ReleaseD2DResources();

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    Zone zone_;
    IconService* icons_ = nullptr;

    ID2D1Factory* factory_ = nullptr;
    ID2D1HwndRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* bgBrush_ = nullptr;
    ID2D1SolidColorBrush* titleBrush_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    IDWriteTextFormat* textFormat_ = nullptr;

    bool dragging_ = false;
    POINT dragStart_{};
    RECT windowStart_{};
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `ZoneWindow.cpp`（绘制与基础交互）**

```cpp
#include "pch.h"
#include "desktopsticker/ZoneWindow.h"
#include "desktopsticker/IconService.h"

#include <dwrite.h>
#include <map>

namespace desktopsticker {

namespace {
const wchar_t kZoneWindowClass[] = L"DesktopSticker.ZoneWindow";
std::map<HWND, ZoneWindow*> g_windows;

void ApplyAcrylic(HWND hwnd) {
    // Win10/11 的 SetWindowCompositionAttribute（动态加载，未文档化）
    enum AccentState { ACCENT_DISABLED = 0, ACCENT_ENABLE_BLURBEHIND = 3, ACCENT_ENABLE_ACRYLICBLURBEHIND = 4 };
    struct AccentPolicy { int state; int flags; int color; int animationId; };
    struct WinCompAttrData { int attribute; void* data; unsigned long size; };

    using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WinCompAttrData*);
    static SetWindowCompositionAttributeFn fn = nullptr;
    if (!fn) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        fn = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
    }
    if (!fn) return;

    AccentPolicy policy{};
    policy.state = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.flags = 2; // 使 color 的 alpha 生效
    policy.color = 0xCC1E1E1E; // BGRA：半透明深色
    WinCompAttrData data{};
    data.attribute = 19; // WCA_ACCENT_POLICY
    data.data = &policy;
    data.size = sizeof(policy);
    fn(hwnd, &data);
}
} // namespace

bool ZoneWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kZoneWindowClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void ZoneWindow::UnregisterClass(HINSTANCE hInst) {
    UnregisterClassW(kZoneWindowClass, hInst);
}

ZoneWindow* ZoneWindow::FromHwnd(HWND hwnd) {
    auto it = g_windows.find(hwnd);
    return it == g_windows.end() ? nullptr : it->second;
}

ZoneWindow::ZoneWindow(HINSTANCE hInst, const Zone& zone, IconService* icons)
    : hInst_(hInst), zone_(zone), icons_(icons) {}

ZoneWindow::~ZoneWindow() {
    Destroy();
    ReleaseD2DResources();
}

bool ZoneWindow::Create(HWND parentOrNull, bool aboveIcons) {
    DWORD style = WS_POPUP | WS_VISIBLE;
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    hwnd_ = CreateWindowExW(exStyle, kZoneWindowClass, L"",
                            style,
                            zone_.rect.left, zone_.rect.top,
                            zone_.rect.right - zone_.rect.left,
                            zone_.rect.bottom - zone_.rect.top,
                            parentOrNull, nullptr, hInst_, this);
    if (!hwnd_) return false;

    g_windows[hwnd_] = this;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // 圆角区域
    HRGN rgn = CreateRoundRectRgn(0, 0,
                                  zone_.rect.right - zone_.rect.left + 1,
                                  zone_.rect.bottom - zone_.rect.top + 1, 16, 16);
    SetWindowRgn(hwnd_, rgn, TRUE);

    ApplyAcrylic(hwnd_);
    return true;
}

void ZoneWindow::Destroy() {
    if (hwnd_) {
        g_windows.erase(hwnd_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void ZoneWindow::SetZone(const Zone& zone) {
    zone_ = zone;
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, zone_.rect.left, zone_.rect.top,
                     zone_.rect.right - zone_.rect.left,
                     zone_.rect.bottom - zone_.rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void ZoneWindow::Refresh() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

bool ZoneWindow::EnsureD2DResources() {
    if (target_) return true;
    if (!factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwriteFactory_));
        if (!factory_ || !dwriteFactory_) return false;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                     D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)),
        &target_);
    if (!target_) return false;

    target_->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E, 0.80f), &bgBrush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), &titleBrush_);
    dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     14.0f, L"zh-cn", &textFormat_);
    return true;
}

void ZoneWindow::ReleaseD2DResources() {
    if (textFormat_) textFormat_->Release();
    if (titleBrush_) titleBrush_->Release();
    if (bgBrush_) bgBrush_->Release();
    if (target_) target_->Release();
    if (dwriteFactory_) dwriteFactory_->Release();
    if (factory_) factory_->Release();
    textFormat_ = nullptr; titleBrush_ = nullptr; bgBrush_ = nullptr;
    target_ = nullptr; dwriteFactory_ = nullptr; factory_ = nullptr;
}

void ZoneWindow::OnPaint() {
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    if (!EnsureD2DResources()) {
        EndPaint(hwnd_, &ps);
        return;
    }

    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0));
    target_->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(1, 1,
                                      static_cast<float>(zone_.rect.right - zone_.rect.left - 1),
                                      static_cast<float>(zone_.rect.bottom - zone_.rect.top - 1)),
                          16.0f, 16.0f),
        bgBrush_, 1.0f);

    // 标题
    std::wstring title = zone_.collapsed ? L"▸ " + zone_.name : L"▾ " + zone_.name;
    target_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), textFormat_,
                       D2D1::RectF(16, 8, 400, 40), titleBrush_);

    // 磁贴（MVP：只画占位矩形 + 图标）
    if (!zone_.collapsed) {
        float x = 16.0f, y = 48.0f;
        for (const auto& path : zone_.itemPaths) {
            HICON icon = icons_ ? icons_->GetIcon(path, 32) : nullptr;
            if (icon) {
                DrawIconEx(target_->GetHwnd(), static_cast<int>(x), static_cast<int>(y),
                           icon, 32, 32, 0, nullptr, DI_NORMAL);
            } else {
                target_->FillRectangle(D2D1::RectF(x, y, x + 32, y + 32),
                                       bgBrush_);
            }
            x += 48.0f;
            if (x + 48 > static_cast<float>(zone_.rect.right - zone_.rect.left)) {
                x = 16.0f;
                y += 48.0f;
            }
        }
    }

    target_->EndDraw();
    EndPaint(hwnd_, &ps);
}

void ZoneWindow::OnLButtonDown(int x, int y) {
    dragging_ = true;
    dragStart_ = POINT{x, y};
    GetWindowRect(hwnd_, &windowStart_);
    SetCapture(hwnd_);

    // 点击标题栏折叠/展开
    if (y < 40) {
        if (onCollapseToggle) onCollapseToggle(zone_.id);
    }
}

void ZoneWindow::OnLButtonUp(int x, int y) {
    dragging_ = false;
    ReleaseCapture();
}

void ZoneWindow::OnMouseMove(int x, int y) {
    if (!dragging_) return;
    POINT pt{};
    GetCursorPos(&pt);
    int dx = pt.x - dragStart_.x;
    int dy = pt.y - dragStart_.y;
    SetWindowPos(hwnd_, nullptr,
                 windowStart_.left + dx, windowStart_.top + dy,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

LRESULT CALLBACK ZoneWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ZoneWindow* self = FromHwnd(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ZoneWindow*>(cs->lpCreateParams);
        g_windows[hwnd] = self;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT:
        self->OnPaint();
        return 0;
    case WM_LBUTTONDOWN:
        self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        self->OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_DESTROY:
        g_windows.erase(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace desktopsticker
```

- [ ] **Step 3: 加入工程并构建**

把 `ZoneWindow.h/.cpp` 加入 Features vcxproj，链接 `d2d1.lib`、`dwrite.lib`、`comctl32.lib`、`shlwapi.lib`（在 vcxproj `<Link><AdditionalDependencies>` 中追加）。构建 Expected: 通过。

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "feat: add Direct2D ZoneWindow with acrylic and tile rendering"
```

---

## Task 15: 桌面图标枚举 + 原生图标移出/恢复

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/DesktopIconManager.h`
- Create: `src/DesktopSticker.Features/src/DesktopIconManager.cpp`
- Modify: Features vcxproj

- [ ] **Step 1: 编写 `DesktopIconManager.h`**

```cpp
#pragma once
#include <map>
#include <string>
#include <vector>
#include <windows.h>

#include "desktopsticker/DesktopShellIntegration.h"

namespace desktopsticker {

struct DesktopIconInfo {
    int index = -1;
    std::wstring label;
    std::wstring path;
    POINT position{};
};

class DesktopIconManager {
public:
    explicit DesktopIconManager(DesktopShellIntegration* shell);

    std::vector<DesktopIconInfo> EnumIcons();
    bool MoveIconOffscreen(int index);
    bool RestoreIcon(int index, POINT position);
    bool SetAutoArrange(bool enable);

    // 由 ShellIntegration 提供 ListView
    HWND ListView() const { return shell_->Windows().listView; }

private:
    std::wstring ResolvePathFromLabel(const std::wstring& label);

    DesktopShellIntegration* shell_;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `DesktopIconManager.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/DesktopIconManager.h"

#include <commctrl.h>
#include <shlobj.h>
#include <set>

namespace desktopsticker {

DesktopIconManager::DesktopIconManager(DesktopShellIntegration* shell) : shell_(shell) {}

std::vector<DesktopIconInfo> DesktopIconManager::EnumIcons() {
    std::vector<DesktopIconInfo> icons;
    HWND lv = ListView();
    if (!lv) return icons;

    const int count = static_cast<int>(SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        wchar_t buf[512]{};
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = buf;
        item.cchTextMax = 512;
        SendMessageW(lv, LVM_GETITEMTEXTW, i, reinterpret_cast<LPARAM>(&item));

        POINT pt{};
        SendMessageW(lv, LVM_GETITEMPOSITION, i, reinterpret_cast<LPARAM>(&pt));

        DesktopIconInfo info;
        info.index = i;
        info.label = buf;
        info.position = pt;
        info.path = ResolvePathFromLabel(buf);
        icons.push_back(std::move(info));
    }
    return icons;
}

bool DesktopIconManager::MoveIconOffscreen(int index) {
    HWND lv = ListView();
    if (!lv) return false;
    return SendMessageW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(-32000, -32000)) != FALSE;
}

bool DesktopIconManager::RestoreIcon(int index, POINT position) {
    HWND lv = ListView();
    if (!lv) return false;
    return SendMessageW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(position.x, position.y)) != FALSE;
}

bool DesktopIconManager::SetAutoArrange(bool enable) {
    HWND lv = ListView();
    if (!lv) return false;
    const LONG_PTR style = GetWindowLongPtrW(lv, GWL_STYLE);
    LONG_PTR newStyle = style;
    if (enable) newStyle |= LVS_AUTOARRANGE;
    else newStyle &= ~LVS_AUTOARRANGE;
    if (newStyle != style) {
        SetWindowLongPtrW(lv, GWL_STYLE, newStyle);
    }
    return true;
}

std::wstring DesktopIconManager::ResolvePathFromLabel(const std::wstring& label) {
    // 获取桌面目录，按显示名匹配
    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
        return L"";
    }
    std::wstring result;
    std::filesystem::path desktop(desktopPath);
    CoTaskMemFree(desktopPath);

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(desktop, ec)) {
        const auto& p = entry.path();
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME)) {
            std::wstring displayName = sfi.szDisplayName;
            // 显示名忽略大小写比较
            if (_wcsicmp(displayName.c_str(), label.c_str()) == 0) {
                result = p.wstring();
                break;
            }
        }
    }
    return result;
}

} // namespace desktopsticker
```

- [ ] **Step 3: 加入工程并构建**

Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "feat: add DesktopIconManager for enumerating and moving native desktop icons"
```

---

## Task 16: DesktopWorkspace 组装 + 自动分类 + 收纳

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/DesktopWorkspace.h`
- Create: `src/DesktopSticker.Features/src/DesktopWorkspace.cpp`
- Modify: `FeatureModule.h/.cpp`（启动 DesktopWorkspace）
- Modify: Features vcxproj

- [ ] **Step 1: 编写 `DesktopWorkspace.h`**

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "desktopsticker/DesktopShellIntegration.h"
#include "desktopsticker/DesktopIconManager.h"
#include "desktopsticker/ZoneModel.h"
#include "desktopsticker/ZoneWindow.h"
#include "desktopsticker/IconService.h"
#include "desktopsticker/ConfigStore.h"

namespace desktopsticker {

class DesktopWorkspace {
public:
    DesktopWorkspace(ConfigStore* config, const std::function<void()>& zonesChanged);
    ~DesktopWorkspace();

    bool Initialize();
    void Shutdown();

    void ToggleCleanDesktop();
    void RestoreDesktop();
    void Refresh();

private:
    std::wstring ClassifyPath(const std::wstring& path);
    void AutoClassify(const std::vector<DesktopIconInfo>& icons);
    void CollectIntoZone(const std::wstring& zoneId, const std::wstring& path, int iconIndex);
    void RemoveFromZone(const std::wstring& zoneId, const std::wstring& path);
    void CreateZoneWindows();
    void DestroyZoneWindows();
    void SaveLayout();

    static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                 UINT_PTR id, DWORD_PTR data);

    ConfigStore* config_;
    std::function<void()> zonesChanged_;
    std::filesystem::path layoutPath_;

    DesktopShellIntegration shell_;
    std::unique_ptr<DesktopIconManager> iconManager_;
    std::unique_ptr<IconService> iconService_;
    ZoneModel model_;
    std::vector<std::unique_ptr<ZoneWindow>> zoneWindows_;
    bool cleanMode_ = false;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `DesktopWorkspace.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/DesktopWorkspace.h"

#include <windowsx.h>
#include <commctrl.h>

namespace fs = std::filesystem;

namespace desktopsticker {

DesktopWorkspace::DesktopWorkspace(ConfigStore* config, const std::function<void()>& zonesChanged)
    : config_(config), zonesChanged_(zonesChanged),
      layoutPath_(config->GetRootDir() / L"layout.json") {}

DesktopWorkspace::~DesktopWorkspace() {
    Shutdown();
}

bool DesktopWorkspace::Initialize() {
    if (!shell_.Initialize()) {
        // 降级：仍创建普通窗口（无 Shell 嵌入），功能可用
    }

    iconManager_ = std::make_unique<DesktopIconManager>(&shell_);
    iconService_ = std::make_unique<IconService>();

    // 记录并关闭自动排列，避免被收纳图标自动回位
    if (iconManager_->ListView()) {
        LONG_PTR style = GetWindowLongPtrW(iconManager_->ListView(), GWL_STYLE);
        model_.Layout().autoArrangeWasEnabled = (style & LVS_AUTOARRANGE) != 0;
        iconManager_->SetAutoArrange(false);
    }

    // 已有布局则加载；否则首次自动分类
    if (!model_.Load(layoutPath_) || model_.Layout().zones.empty()) {
        auto icons = iconManager_->EnumIcons();
        AutoClassify(icons);
        SaveLayout();
    }

    // 把已收纳的原生图标移到屏幕外（启动恢复场景）
    for (const auto& zone : model_.Layout().zones) {
        for (const auto& path : zone.itemPaths) {
            auto icons = iconManager_->EnumIcons();
            for (const auto& icon : icons) {
                if (_wcsicmp(icon.path.c_str(), path.c_str()) == 0) {
                    iconManager_->MoveIconOffscreen(icon.index);
                    break;
                }
            }
        }
    }

    CreateZoneWindows();
    shell_.SubclassListView(ListViewSubclassProc, 1,
                            reinterpret_cast<DWORD_PTR>(this));
    return true;
}

void DesktopWorkspace::Shutdown() {
    shell_.UnsubclassListView(ListViewSubclassProc, 1);
    DestroyZoneWindows();
    RestoreDesktop();
    iconService_->ClearCache();
    iconManager_.reset();
    shell_.Shutdown();
}

void DesktopWorkspace::Refresh() {
    for (auto& w : zoneWindows_) w->Refresh();
    if (zonesChanged_) zonesChanged_();
}

std::wstring DesktopWorkspace::ClassifyPath(const std::wstring& path) {
    // 简单规则：快捷方式 → 应用；否则按扩展名
    fs::path p(path);
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (ext == L".lnk" || ext == L".exe" || ext == L".appref-ms") return L"应用";
    if (ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".gif" || ext == L".bmp" || ext == L".webp") return L"图片";
    if (ext == L".doc" || ext == L".docx" || ext == L".pdf" || ext == L".txt" || ext == L".md" || ext == L".xls" || ext == L".xlsx" || ext == L".ppt" || ext == L".pptx") return L"文档";
    if (ext == L".mp4" || ext == L".avi" || ext == L".mkv" || ext == L".mov" || ext == L".wmv") return L"视频";
    if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".aac") return L"音乐";
    if (fs::is_directory(p)) return L"文件夹";
    return L"其他";
}

void DesktopWorkspace::AutoClassify(const std::vector<DesktopIconInfo>& icons) {
    // 预置分区
    const wchar_t* names[] = {L"应用", L"文档", L"图片", L"视频", L"音乐", L"文件夹", L"其他"};
    int monitorIndex = 0;
    int x = 40;
    int y = 60;
    for (const wchar_t* name : names) {
        Zone z;
        z.id = model_.GenerateZoneId();
        z.name = name;
        z.monitorIndex = monitorIndex;
        z.rect = RECT{x, y, x + 340, y + 240};
        x += 380;
        if (x > 1400) { x = 40; y += 280; }
        model_.AddZone(std::move(z));
    }

    for (const auto& icon : icons) {
        if (icon.path.empty()) continue;
        std::wstring category = ClassifyPath(icon.path);
        Zone* zone = model_.FindZone(category);
        if (!zone) continue;
        zone->itemPaths.push_back(icon.path);
        model_.Layout().originalIconPositions[icon.path] = icon.position;
        iconManager_->MoveIconOffscreen(icon.index);
    }
}

void DesktopWorkspace::CreateZoneWindows() {
    ZoneWindow::RegisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
    for (const auto& zone : model_.Layout().zones) {
        auto win = std::make_unique<ZoneWindow>(GetModuleHandleW(L"DesktopSticker.Features.dll"),
                                                zone, iconService_.get());
        win->onCollapseToggle = [this](const std::wstring& zoneId) {
            if (Zone* z = model_.FindZone(zoneId)) {
                z->collapsed = !z->collapsed;
                SaveLayout();
                Refresh();
            }
        };
        // shell_ 嵌入失败时 parentOrNull=nullptr → 普通顶层窗口（降级模式）
        if (shell_.IsReady()) {
            win->Create(nullptr, true);
            shell_.EmbedWindow(win->Hwnd(), true);
        } else {
            win->Create(nullptr, true);
            SetWindowPos(win->Hwnd(), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        zoneWindows_.push_back(std::move(win));
    }
}

void DesktopWorkspace::DestroyZoneWindows() {
    zoneWindows_.clear();
    ZoneWindow::UnregisterClass(GetModuleHandleW(L"DesktopSticker.Features.dll"));
}

void DesktopWorkspace::ToggleCleanDesktop() {
    cleanMode_ = !cleanMode_;
    for (auto& w : zoneWindows_) {
        ShowWindow(w->Hwnd(), cleanMode_ ? SW_HIDE : SW_SHOW);
    }
    // 原生图标：干净模式全部移出屏幕外；恢复模式还原
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        if (cleanMode_) {
            iconManager_->MoveIconOffscreen(icon.index);
        } else {
            auto it = model_.Layout().originalIconPositions.find(icon.path);
            if (it != model_.Layout().originalIconPositions.end()) {
                iconManager_->RestoreIcon(icon.index, it->second);
            }
        }
    }
}

void DesktopWorkspace::RestoreDesktop() {
    if (!iconManager_) return;
    auto icons = iconManager_->EnumIcons();
    for (const auto& icon : icons) {
        auto it = model_.Layout().originalIconPositions.find(icon.path);
        if (it != model_.Layout().originalIconPositions.end()) {
            iconManager_->RestoreIcon(icon.index, it->second);
        }
    }
    if (model_.Layout().autoArrangeWasEnabled) {
        iconManager_->SetAutoArrange(true);
    }
}

void DesktopWorkspace::SaveLayout() {
    model_.Save(layoutPath_);
}

LRESULT CALLBACK DesktopWorkspace::ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                        UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<DesktopWorkspace*>(data);
    if (msg == WM_LBUTTONDBLCLK) {
        LVHITTESTINFO ht{};
        ht.pt = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int index = static_cast<int>(SendMessageW(hwnd, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(&ht)));
        if (index == -1) { // 空白处
            self->ToggleCleanDesktop();
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

} // namespace desktopsticker
```

- [ ] **Step 3: 在 `FeatureModule` 中启动 DesktopWorkspace**

在 `FeatureModule.h` 增加 `#include "desktopsticker/DesktopWorkspace.h"` 与成员 `std::unique_ptr<DesktopWorkspace> workspace_;`；在 `Init` 中创建（此时需要 `ConfigStore` 与 `IndexService` 已初始化），在 `Start` 中调用 `workspace_->Initialize()`，在 `Stop` 中 `workspace_->Shutdown()`。

- [ ] **Step 4: 加入工程并构建**

Expected: 编译通过。

- [ ] **Step 5: 手动验收**

运行工具：桌面上出现自动分类分区（应用/文档/图片…），原生图标被移出可视区，分区内显示磁贴。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add DesktopWorkspace with auto-classification and icon collection"
```

---

## Task 17: 折叠/展开 + 拖拽换分区 + IDropTarget

**Files:**
- Modify: `src/DesktopSticker.Features/src/ZoneWindow.cpp`（折叠点击已实现，补拖拽换分区）
- Create: `src/DesktopSticker.Features/include/desktopsticker/DropTarget.h`
- Create: `src/DesktopSticker.Features/src/DropTarget.cpp`
- Modify: `DesktopWorkspace.cpp`（注册 DropTarget、处理拖放）

- [ ] **Step 1: 编写 `DropTarget.h`**

```cpp
#pragma once
#include <shobjidl.h>
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace desktopsticker {

class DropTarget : public IDropTarget {
public:
    using DropCallback = std::function<void(const std::vector<std::wstring>& paths)>;

    DropTarget(HWND hwnd, DropCallback callback);
    virtual ~DropTarget() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

private:
    static std::vector<std::wstring> GetPaths(IDataObject* data);

    HWND hwnd_;
    DropCallback callback_;
    ULONG refCount_ = 1;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `DropTarget.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/DropTarget.h"

#include <shellapi.h>
#include <shlobj.h>

namespace desktopsticker {

DropTarget::DropTarget(HWND hwnd, DropCallback callback)
    : hwnd_(hwnd), callback_(std::move(callback)) {}

STDMETHODIMP DropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DropTarget::AddRef() { return ++refCount_; }
STDMETHODIMP_(ULONG) DropTarget::Release() {
    ULONG r = --refCount_;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP DropTarget::DragEnter(IDataObject*, DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP DropTarget::DragOver(DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

STDMETHODIMP DropTarget::DragLeave() { return S_OK; }

STDMETHODIMP DropTarget::Drop(IDataObject* data, DWORD, POINTL, DWORD* pdwEffect) {
    *pdwEffect = DROPEFFECT_COPY;
    auto paths = GetPaths(data);
    if (!paths.empty() && callback_) callback_(paths);
    return S_OK;
}

std::vector<std::wstring> DropTarget::GetPaths(IDataObject* data) {
    std::vector<std::wstring> paths;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&fmt, &medium))) return paths;

    HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    if (drop) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t buf[MAX_PATH]{};
            DragQueryFileW(drop, i, buf, MAX_PATH);
            paths.emplace_back(buf);
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return paths;
}

} // namespace desktopsticker
```

- [ ] **Step 3: 在 `DesktopWorkspace::CreateZoneWindows` 中注册 DropTarget**

每个 `ZoneWindow` 创建后：

```cpp
auto drop = new DropTarget(win->Hwnd(), [this, zoneId = zone.id](const std::vector<std::wstring>& paths) {
    Zone* zone = model_.FindZone(zoneId);
    if (!zone) return;
    for (const auto& path : paths) {
        // 如果该路径已在布局中（被收纳），只更新归属；否则新增收纳
        zone->itemPaths.push_back(path);
        // 把原生图标移出屏幕（若存在）
        auto icons = iconManager_->EnumIcons();
        for (const auto& icon : icons) {
            if (_wcsicmp(icon.path.c_str(), path.c_str()) == 0) {
                if (model_.Layout().originalIconPositions.find(path) == model_.Layout().originalIconPositions.end()) {
                    model_.Layout().originalIconPositions[path] = icon.position;
                }
                iconManager_->MoveIconOffscreen(icon.index);
            }
        }
    }
    SaveLayout();
    Refresh();
});
RegisterDragDrop(win->Hwnd(), drop);
```

注意：`DropTarget` 通过 `Release` 自毁；窗口销毁前需 `RevokeDragDrop(hwnd)`。在 `DestroyZoneWindows()` 中遍历调用 `RevokeDragDrop`。

- [ ] **Step 4: 拖拽磁贴换分区**

在 `ZoneWindow` 中，当鼠标从磁贴按下并拖出分区时，把磁贴路径记录为“拖拽项”，在 `WM_MOUSEMOVE` 超过阈值后调用 `onItemDrag(zoneId, path)`；`DesktopWorkspace` 注册该回调，用 `MoveItem` 更新归属并刷新。具体代码：

```cpp
// ZoneWindow.h 增加
std::function<void(const std::wstring& zoneId, const std::wstring& itemPath)> onItemDrag;

// ZoneWindow.cpp OnLButtonDown：命中磁贴时记录
draggingItem_ = HitTestItem(x, y); // 返回磁贴路径，空表示空白

// OnMouseMove：若 draggingItem_ 非空且位移 > 8px，触发 onItemDrag
if (!draggingItem_.empty() && onItemDrag) {
    POINT pt; GetCursorPos(&pt);
    if (abs(pt.x - dragStart_.x) + abs(pt.y - dragStart_.y) > 8) {
        onItemDrag(zone_.id, draggingItem_);
        draggingItem_.clear();
    }
}
```

`DesktopWorkspace` 回调：

```cpp
win->onItemDrag = [this](const std::wstring& fromZoneId, const std::wstring& itemPath) {
    // MVP：拖到另一个分区窗口的 DropTarget 由系统拖放完成；
    // 这里先实现简单移除（从原分区删除并恢复原生图标）
    Zone* from = model_.FindZone(fromZoneId);
    if (!from) return;
    auto it = std::find(from->itemPaths.begin(), from->itemPaths.end(), itemPath);
    if (it != from->itemPaths.end()) {
        from->itemPaths.erase(it);
        // 恢复原生图标
        auto icons = iconManager_->EnumIcons();
        for (const auto& icon : icons) {
            if (_wcsicmp(icon.path.c_str(), itemPath.c_str()) == 0) {
                auto posIt = model_.Layout().originalIconPositions.find(itemPath);
                if (posIt != model_.Layout().originalIconPositions.end()) {
                    iconManager_->RestoreIcon(icon.index, posIt->second);
                }
            }
        }
        SaveLayout();
        Refresh();
    }
};
```

说明：跨分区拖放最稳妥的做法是使用 OLE 拖放（`DoDragDrop`）；MVP 阶段先实现“拖出即移回桌面”，完整拖入另一分区在 Task 21 的 DropTarget 基础上扩展。

- [ ] **Step 5: 构建 + 验收**

构建通过；运行后折叠/展开分区标题、把文件拖到分区窗口内被收纳、拖出分区恢复图标。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add zone collapse, item drag-out, and shell drop target"
```

---

## Task 18: 右键菜单 + 双击空白 + 移动/缩放

**Files:**
- Modify: `DesktopWorkspace.cpp`（右键菜单、双击空白已含子类化）
- Modify: `ZoneWindow.cpp`（右键菜单回调、边缘缩放）

- [ ] **Step 1: 分区标题/磁贴右键菜单**

在 `ZoneWindow::WndProc` 处理 `WM_RBUTTONUP`：

```cpp
case WM_RBUTTONUP: {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ClientToScreen(hwnd, &pt);
    HMENU menu = CreatePopupMenu();
    if (pt.y < 40) {
        // 标题菜单
        AppendMenuW(menu, MF_STRING, 1, L"重命名");
        AppendMenuW(menu, MF_STRING, 2, L"新建分区");
        AppendMenuW(menu, MF_STRING, 3, L"删除分区");
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        if (cmd == 1 && self->onRenameZone) self->onRenameZone(self->zone_.id);
        if (cmd == 3 && self->onDeleteZone) self->onDeleteZone(self->zone_.id);
    } else {
        // 磁贴菜单（MVP：对第一个磁贴）
        std::wstring item = self->HitTestItem(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (!item.empty() && self->onItemContextMenu) {
            AppendMenuW(menu, MF_STRING, 1, L"打开");
            AppendMenuW(menu, MF_STRING, 2, L"从分区移出");
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            if (cmd == 1) ShellExecuteW(nullptr, L"open", item.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (cmd == 2) self->onRemoveItem(self->zone_.id, item);
        }
    }
    DestroyMenu(menu);
    return 0;
}
```

对应在 `ZoneWindow.h` 增加回调：`onRenameZone`、`onDeleteZone`、`onRemoveItem`，并在 `DesktopWorkspace::CreateZoneWindows` 中实现（重命名用 `InputDialog` 或简化固定名，MVP 允许直接改名）。

- [ ] **Step 2: 双击桌面空白干净桌面**

`DesktopWorkspace::ListViewSubclassProc` 已在 Task 16 实现（`WM_LBUTTONDBLCLK` + `LVM_HITTEST == -1` → `ToggleCleanDesktop`）。验证即可；若分区窗口遮挡空白，需在 `ZoneWindow::WM_NCHITTEST` 返回 `HTTRANSPARENT` 让空白区域点击穿透到桌面：

```cpp
case WM_NCHITTEST: {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &pt);
    // 仅标题栏和磁贴区域视为可点击，其余返回 HTTRANSPARENT
    if (pt.y < 40 || !self->HitTestItem(pt.x, pt.y).empty()) return HTCLIENT;
    return HTTRANSPARENT;
}
```

- [ ] **Step 3: 分区边缘缩放**

在 `ZoneWindow` 的 `WM_NCHITTEST` 中根据鼠标在边缘 8px 内返回 `HTLEFT/HTRIGHT/HTTOP/HTBOTTOM/HTTOPLEFT/...`，系统自动处理缩放（`WS_THICKFRAME` 风格需要在 `CreateWindowEx` 中添加；但为保持无边框，也可手动处理 `WM_SIZING`）。MVP 采用给窗口加 `WS_THICKFRAME` 的隐藏边框方式，并在 `WM_NCCALCSIZE` 返回 0 隐藏系统边框：

```cpp
// CreateWindowEx style 增加 WS_THICKFRAME
// WndProc 增加：
case WM_NCCALCSIZE:
    if (wp) return 0;
    break;
```

- [ ] **Step 4: 构建 + 验收**

运行后验证：分区标题右键菜单可重命名/删除；磁贴右键可打开/移出；双击桌面空白切换干净模式；拖动分区边缘可缩放。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: add zone context menus, clean-desktop toggle, and resize"
```

---

## Task 19: 文件监听自动归类 + 一键恢复 + 降级模式

**Files:**
- Create: `src/DesktopSticker.Features/include/desktopsticker/DirectoryWatcher.h`
- Create: `src/DesktopSticker.Features/src/DirectoryWatcher.cpp`
- Modify: `DesktopWorkspace.cpp`（监听桌面目录、新图标自动归类）
- Modify: `FeatureModule.cpp`（暴露一键恢复接口）

- [ ] **Step 1: 编写 `DirectoryWatcher.h`**

```cpp
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>

namespace desktopsticker {

class DirectoryWatcher {
public:
    using Callback = std::function<void()>;

    DirectoryWatcher(std::filesystem::path dir, Callback callback);
    ~DirectoryWatcher();

    bool Start();
    void Stop();

private:
    void ThreadMain();

    std::filesystem::path dir_;
    Callback callback_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace desktopsticker
```

- [ ] **Step 2: 编写 `DirectoryWatcher.cpp`**

```cpp
#include "pch.h"
#include "desktopsticker/DirectoryWatcher.h"

namespace desktopsticker {

DirectoryWatcher::DirectoryWatcher(std::filesystem::path dir, Callback callback)
    : dir_(std::move(dir)), callback_(std::move(callback)) {}

DirectoryWatcher::~DirectoryWatcher() {
    Stop();
}

bool DirectoryWatcher::Start() {
    if (running_.exchange(true)) return false;
    handle_ = CreateFileW(dir_.c_str(), FILE_LIST_DIRECTORY,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        running_.store(false);
        return false;
    }
    thread_ = std::thread([this]() { ThreadMain(); });
    return true;
}

void DirectoryWatcher::Stop() {
    if (!running_.exchange(false)) return;
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (thread_.joinable()) thread_.join();
}

void DirectoryWatcher::ThreadMain() {
    alignas(FILE_NOTIFY_INFORMATION) char buffer[64 * 1024];
    while (running_.load()) {
        DWORD bytes = 0;
        if (!ReadDirectoryChangesW(handle_, buffer, sizeof(buffer), FALSE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION,
                                   &bytes, nullptr, nullptr)) {
            break;
        }
        if (callback_) callback_();
    }
}

} // namespace desktopsticker
```

- [ ] **Step 3: 接入 `DesktopWorkspace`**

在 `DesktopWorkspace` 增加成员 `std::unique_ptr<DirectoryWatcher> desktopWatcher_;`；`Initialize()` 中创建并启动：

```cpp
PWSTR desktopPath = nullptr;
if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
    desktopWatcher_ = std::make_unique<DirectoryWatcher>(desktopPath, [this]() {
        // 桌面变化：新图标自动归类
        auto icons = iconManager_->EnumIcons();
        for (const auto& icon : icons) {
            if (icon.path.empty()) continue;
            bool alreadyCollected = false;
            for (const auto& zone : model_.Layout().zones) {
                for (const auto& p : zone.itemPaths) {
                    if (_wcsicmp(p.c_str(), icon.path.c_str()) == 0) {
                        alreadyCollected = true;
                        break;
                    }
                }
                if (alreadyCollected) break;
            }
            if (alreadyCollected) continue;
            // 新图标按规则归类
            std::wstring category = ClassifyPath(icon.path);
            Zone* zone = model_.FindZone(category);
            if (zone) {
                zone->itemPaths.push_back(icon.path);
                model_.Layout().originalIconPositions[icon.path] = icon.position;
                iconManager_->MoveIconOffscreen(icon.index);
            }
        }
        SaveLayout();
        Refresh();
    });
    desktopWatcher_->Start();
    CoTaskMemFree(desktopPath);
}
```

`Shutdown()` 中 `desktopWatcher_->Stop()`。

- [ ] **Step 4: 一键恢复 + 降级模式**

在 `IFeatureModule` 增加 `virtual void RestoreDesktop() = 0;`，`FeatureModule` 转发到 `workspace_->RestoreDesktop()`。`RestoreDesktop` 已实现（Task 16）：恢复原生图标坐标 + 自动排列设置。

降级模式已在 `DesktopWorkspace::Initialize()` 中实现（`shell_.IsReady()` 为 false 时创建普通置底窗口）。在 `FeatureModule::Start()` 中若 `!workspace_->Initialize()` 则写日志并继续运行启动器功能。

- [ ] **Step 5: 构建 + 验收**

向桌面复制一个新文件，观察自动进入对应分区；调用恢复接口（托盘菜单“恢复桌面”）后原生图标全部回到原位置。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: add desktop watcher auto-classification, restore, and fallback mode"
```

---

## Task 20: 集成验收 + 打包 + 自启动

**Files:**
- Create: `README.md`（简短使用说明）
- Modify: `DesktopSticker.App` 的 `Package.appxmanifest`（可选打包）
- Modify: `MainWindow.xaml.cpp`（托盘“退出”时 `UnloadFeatures`）

- [ ] **Step 1: 编写 README**

```markdown
# Desktop Sticker

Windows 11 桌面分区收纳 + 双击空格搜索启动器。

## 使用
- 双击空格：唤起/隐藏搜索启动器
- 桌面分区：首次自动分类，可折叠/拖拽/右键管理
- 双击桌面空白：切换干净桌面
- 托盘图标：设置 / 恢复桌面 / 退出

## 构建
使用 Visual Studio 2022（C++ 桌面开发 + Windows 11 SDK）打开 `DesktopSticker.sln`，x64 Debug/Release 构建。

## 数据
配置与布局保存在 `%APPDATA%\DesktopSticker\`。
```

- [ ] **Step 2: 托盘“退出”完整清理**

在 `MainWindow.xaml.cpp` 托盘回调中处理 `WM_APP + 1`：

```cpp
case WM_APP + 1:
    if (LOWORD(lParam) == WM_RBUTTONUP) {
        // 弹出菜单：设置 / 恢复桌面 / 退出
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"设置");
        AppendMenuW(menu, MF_STRING, 2, L"恢复桌面");
        AppendMenuW(menu, MF_STRING, 3, L"退出");
        POINT pt; GetCursorPos(&pt);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, nid.hWnd, nullptr);
        DestroyMenu(menu);
        if (cmd == 3) {
            // 通知 App 退出并 UnloadFeatures
            PostMessageW(nid.hWnd, WM_CLOSE, 0, 0);
        }
    }
    break;
```

- [ ] **Step 3: 完整构建 + 全量测试**

```bash
cmd.exe /c "\"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild DesktopSticker.sln /p:Configuration=Release /p:Platform=x64 /m && \"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe\" bin\x64\Release\Tests\DesktopSticker.Tests.dll"
```

Expected: Release 构建成功，全部单元测试通过。

- [ ] **Step 4: 手动验收清单**

逐项勾选：

- [ ] 首次启动自动创建分区并收纳桌面图标
- [ ] 分区折叠/展开正常
- [ ] 拖拽磁贴换分区（或拖出恢复）
- [ ] 原生图标拖入分区被收纳
- [ ] 右键菜单：打开/移出/重命名/删除分区
- [ ] 双击桌面空白切换干净桌面
- [ ] 双击空格唤起/隐藏搜索启动器
- [ ] 搜索中文与拼音首字母（如“微信”/“wx”）
- [ ] 设置窗口：热键、搜索范围、添加应用
- [ ] 一键恢复桌面
- [ ] 多显示器下分区坐标正确
- [ ] 退出后无残留分区窗口、原生图标全部恢复

- [ ] **Step 5: 自启动（可选，注册表 Run 键）**

```bash
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "DesktopSticker" /t REG_SZ /d "\"D:\project\Desktop Sticker\bin\x64\Release\DesktopSticker.App\DesktopSticker.App.exe\"" /f
```

- [ ] **Step 6: 最终提交**

```bash
git add -A
git commit -m "feat: finalize packaging, tray menu, and README"
```

---

## 自审记录

- **规格覆盖**：设计文档 8 节全部映射到 Task 1-20（架构→Task 3/10；分区→Task 13-19；启动器→Task 7/11/12；配置→Task 4/9；错误处理与降级→Task 16/19；测试→Task 4-9/20）。
- **占位检查**：所有代码步骤均给出具体代码；UI 步骤中说明 MVP 简化绑定的地方都有“必须保证可用”的验收标准。
- **类型一致性**：`IFeatureModule` 在 Task 11/12 扩展的 `Search`/`AddApp`/`GetConfig`/`SetConfig`/`GetApps`/`RestoreDesktop` 与 `Host`/`FeatureModule` 的实现一致；`ZoneWindow` 回调命名在 Task 14/17/18 保持一致。
