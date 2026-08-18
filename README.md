# Desktop Sticker

Windows 11 桌面分区收纳 + 双击空格搜索启动器。

## 功能

- **桌面分区收纳**：首次自动分类桌面图标（应用/文档/图片/视频/音乐/文件夹/其他），支持折叠/展开、拖拽换分区、右键管理、双击桌面空白切换干净桌面、分区自由移动与缩放。
- **搜索启动器**：双击空格唤起，搜索桌面内容、文档/下载/图片/视频/音乐目录，以及手动添加的应用；支持中文拼音首字母搜索（如 `wx` → 微信）。
- **设置**：托盘右键菜单可打开设置（搜索范围、热键方案、手动应用列表）、一键恢复桌面。

## 构建

环境：Visual Studio 2022（C++ 桌面开发 + Windows 11 SDK + Windows App SDK）。

1. 打开 `Desktop Sticker/Desktop Sticker.sln`
2. 选择 `x64` / `Release`
3. 生成解决方案
4. 运行 `Desktop Sticker/x64/Release/Desktop Sticker/Desktop_Sticker.exe`

> 运行前请确保 `DesktopSticker.Features.dll` 与 EXE 位于同一目录（构建后手动复制，或后续配置自动复制）。

## 测试

```bash
"D:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "D:/project/Desktop Sticker/Desktop Sticker/Desktop Sticker.sln" \
  -p:Configuration=Release -p:Platform=x64 -m:1
"D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release/Tests/DesktopSticker.Tests.exe"
```

## 数据

配置与布局保存在 `%APPDATA%\DesktopSticker\`（`config.json`、`apps.json`、`layout.json`）。

## 已知说明

- 桌面分区使用 Wallpaper Engine 同款 Shell 嵌入技术（`Progman`/`WorkerW`/`SHELLDLL_DefView`），依赖未文档化行为；若嵌入失败会自动降级为普通置底窗口。
- 本机 Debug 配置的测试 exe 启动崩溃（环境级 Debug CRT 问题），测试统一在 Release 下执行。
