# AGENTS.md

Windows 11 desktop organizer: groups desktop icons into movable zone cards, plus a double-space search launcher. WinUI 3 (C++/WinRT) tray app + Win32/Direct2D feature DLL. README, code comments, and UI strings are in Chinese.

## Layout (note the nested same-name directories)

- `Desktop Sticker/Desktop Sticker.sln` — solution (one level down from repo root; paths in docs are absolute and include the nested dir twice).
- `Desktop Sticker/Desktop Sticker/` — WinUI 3 EXE project: `App`, `MainWindow`, `LauncherController`, `SettingsController`, `Host`. `Host` loads the Features DLL at runtime via `LoadLibrary`.
- `Desktop Sticker/DesktopSticker.Features/` — plain C++20 Win32 DLL (Direct2D/DirectWrite rendering, no WinRT/XAML in its pch): zones, desktop icon management, search index, pinyin, hotkey, config.
- `Desktop Sticker/DesktopSticker.Tests/` — unit tests using the homegrown framework in `test_framework.h` (`namespace dtest`, no gtest).
- `Desktop Sticker/Desktop Sticker (Package)/` — MSIX packaging project; the app actually runs unpackaged/self-contained, don't rely on package identity.
- `third_party/nlohmann/json.hpp` — only third-party dependency (include root is `third_party/`).
- `docs/acceptance.md` — manual acceptance checklist; read before/after touching zone or launcher behavior.

## Build & test (Release x64 only)

一键编译：仓库根目录 `build.bat`（双击或 `build.bat test`，含还原、编译、关停运行中的实例、可选跑测试）。

手动编译：No cmake; build with MSBuild (VS2022):

```bash
"D:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "D:/project/Desktop Sticker/Desktop Sticker/Desktop Sticker.sln" \
  -p:Configuration=Release -p:Platform=x64 -m:1
```

Run tests:

```bash
cd "D:/project/Desktop Sticker/Desktop Sticker/bin/x64/Release"
cp DesktopSticker.Features.dll Tests/   # only if PostBuildEvent didn't already
./Tests/DesktopSticker.Tests.exe        # expect: 13 passed, 0 failed
```

Gotchas:
- The Debug test exe crashes on this machine (Debug CRT environment issue) — always test in Release.
- `DesktopSticker.Features.dll` must sit next to the EXE and next to the tests exe; the PostBuildEvent only copies it to the main EXE's OutDir.
- `bin/`, `obj/`, `packages/`, `Generated Files/` are gitignored; NuGet restore is needed for a fresh checkout (packages.config).

## Architecture boundary (keep it)

- EXE ↔ DLL communicate **only** through `desktopsticker::IFeatureModule` (`DesktopSticker.Features/include/desktopsticker/IFeatureModule.h`) plus the extern "C" exports `CreateFeatureModule` / `DestroyFeatureModule`. No other header of the DLL is consumed by the EXE.
- DLL → EXE notifications go through the `FeatureEvents` callbacks (`hotkeyTriggered`, `indexUpdated`, `zonesChanged`).
- The Features DLL is pure Win32 + Direct2D with `WIN32_LEAN_AND_MEAN`/`NOMINMAX` in its pch; don't drag WinRT into it, and don't link the WinUI app into it.

## Fragile areas / known gotchas

- **Desktop shell embedding**: zones dock to the desktop via `Progman`/`WorkerW`/`SHELLDLL_DefView` (Wallpaper-Engine-style, undocumented); on failure the code degrades to a bottom-most normal window. Changes here must keep both paths working.
- **Zone window mouse input** has regressed repeatedly (see git log): making zone windows transparent-for-hit-test breaks clicks; layered/child window positioning and the low-level hook forwarding of button events are load-bearing. Test dragging, wheel scrolling, and click-to-open by hand after touching `ZoneWindow*`/`DesktopWorkspace`/`DropTarget`.
- **Config files** live in `%APPDATA%\DesktopSticker\` (`config.json`, `apps.json`, `layout.json`) and must tolerate being missing/corrupt (auto-rebuild, no crash).
- **WinUI sub-window lifecycle**: clicking the title bar X (or any `WM_CLOSE`) **destroys** the XAML `Window` object — it is not "hide". Every secondary window (`SettingsController`, `LauncherController`) must subscribe `Closed`, set a `closed_` flag, and rebuild the whole window on the next `Show`; calling `Show`/`AppWindow()` on a destroyed window stalls and throws (crash via tray subclass proc). `Show`/`Hide` must be try/caught because tray menu paths run inside native window procedures.
- Zones must also survive monitor/resolution changes; tray "Exit" must restore all native desktop icons and leave no stray zone windows.
- App is single-instance; hotkey is double-Space and must not fire while a text input has focus.
- Recent layout data may predate newer features — loaders auto-migrate old layouts (e.g. auto-expand, default spacing).

## Conventions

- C++20, precompiled headers (`pch.h`) per project; new files must be added to the `.vcxproj` (and `.filters`) — there's no globbing.
- Commit messages: lowercase English `feat:`/`fix:` one-liners summarizing behavior (match existing style).
- Comments/UI strings in Chinese are fine; keep identifiers in English.
