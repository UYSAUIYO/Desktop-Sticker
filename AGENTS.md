# AGENTS.md

Windows 11 desktop organizer: groups desktop icons into movable zone cards, plus a double-space search launcher and a desktop clock widget (analog dial / sunrise-sunset orbit / live weather). WinUI 3 (C++/WinRT) tray app + Win32/Direct2D feature DLL. README, code comments, and UI strings are in Chinese.

## Layout (note the nested same-name directories)

- `Desktop Sticker/Desktop Sticker.sln` — solution (one level down from repo root; paths in docs are absolute and include the nested dir twice).
- `Desktop Sticker/Desktop Sticker/` — WinUI 3 EXE project: `App`, `MainWindow`, `LauncherController`, `SettingsController`, `Host`. `Host` loads the Features DLL at runtime via `LoadLibrary`. `assets/weather/S2/` holds the QWeather icon set (61 PNGs, CC BY 4.0, license text next to it).
- `Desktop Sticker/DesktopSticker.Features/` — plain C++20 Win32 DLL (Direct2D/DirectWrite rendering, no WinRT/XAML in its pch): zones, desktop icon management, search index, pinyin, hotkey, config. `src/widgets/` has the clock + weather component.
- `Desktop Sticker/DesktopSticker.Tests/` — unit tests using the homegrown framework in `test_framework.h` (`namespace dtest`, no gtest).
- `Desktop Sticker/DesktopSticker.WallPaper/` — dynamic wallpaper DLL: D3D11 + DXGI + DirectComposition presentation, MF-primary decode with an FFmpeg shared-library fallback, media library, storage placement. Strategy logic lives in **header-only pure functions** under `include/desktopsticker/wallpaper/` so `dtest` can unit-test it without D3D/MF; OS adapters (drive enumeration, fullscreen detection, subprocess) live in `src/`.
- `Desktop Sticker/DesktopSticker.ResMon/` — read-only resource manager DLL (WebView2 hosted in a plain Win32 window): per-thread CPU, per-module memory, ordered storage classification. Formatting/classification/cpu-math/JSON assembly are **header-only pure functions** under `include/desktopsticker/resmon/`.
- `Desktop Sticker/Desktop Sticker/resmon/` — the resource manager frontend: plain `index.html` / `style.css` / `app.js`, **no framework and no build step**; xcopied next to the EXE and served via the WebView2 virtual host `resmon.local`.
- `Desktop Sticker/Desktop Sticker (Package)/` — MSIX packaging project; the app actually runs unpackaged/self-contained, don't rely on package identity.
- `third_party/nlohmann/json.hpp` — only third-party dependency (include root is `third_party/`). `third_party/` also holds the FFmpeg/OpenH264/MotionWallpaper notices.
- `tools/*.ps1` — PowerShell UI harness for manual verification (see below). `tools/prepare_ffmpeg.ps1` fetches the pinned FFmpeg payload (see below).
- `docs/acceptance.md` — manual acceptance checklist; read before/after touching zone or launcher behavior. `docs/superpowers/` holds the original plan/spec.
- `THIRD_PARTY_NOTICES.md` — third-party disclosure (FFmpeg LGPL v3, OpenH264 BSD, MotionWallpaper MIT).

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
cp DesktopSticker.WallPaper.dll Tests/  # ditto
./Tests/DesktopSticker.Tests.exe        # expect: 200 passed, 0 failed
```

FFmpeg payload (dynamic wallpaper's decoder fallback + transcode backend):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_ffmpeg.ps1
```

Downloads the pinned BtbN LGPL shared build into `tools/ffmpeg/` (runtime) and extracts its
headers into `tools/ffmpeg-sdk/` (build-time, for `decltype(&av_*)` declarations). Both are
gitignored and SHA-256 verified. **The build still succeeds without them** — the FFmpeg path is
gated by `__has_include` and degrades to "fallback decoder unavailable". PowerShell reads `.ps1`
as GBK unless the file has a UTF-8 BOM, so keep the BOM on scripts containing Chinese comments.

Output paths are split (both gitignored):
- EXE → `Desktop Sticker/x64/Release/Desktop Sticker/Desktop_Sticker.exe`
- Features DLL + tests → `Desktop Sticker/bin/x64/Release/` (and `.../Release/Tests/`)

Gotchas:
- The Debug test exe crashes on this machine (Debug CRT environment issue) — always test in Release.
- `DesktopSticker.Features.dll` and `DesktopSticker.WallPaper.dll` must sit next to the EXE and next to the tests exe. The EXE's `PostBuildEvent` copies them from `bin\$(Platform)\$(Configuration)\` to OutDir, and `build.bat test` copies the Features one into `Tests\`.
- The same PostBuildEvent also xcopies `assets\weather\` and `tools\ffmpeg\` next to the EXE; if weather icons or the wallpaper fallback vanish at runtime it's this step, not the code.
- Don't name a header `desktopsticker/Export.h` in a new project: the EXE includes several DLLs' headers, and an identical relative path means `#pragma once` silently skips the second one. WallPaper uses `WallPaperExport.h` for this reason.
- `bin/`, `obj/`, `packages/`, `Generated Files/`, `x64/` are gitignored; NuGet restore is needed for a fresh checkout (packages.config).

## Architecture boundary (keep it)

- EXE ↔ DLL communicate **only** through `desktopsticker::IFeatureModule` (`DesktopSticker.Features/include/desktopsticker/IFeatureModule.h`) plus the extern "C" exports `CreateFeatureModule` / `DestroyFeatureModule`. No other header of the DLL is consumed by the EXE. The interface is the ABI: any field/virtual change means both sides must be rebuilt together.
- DLL → EXE notifications go through the `FeatureEvents` callbacks (`hotkeyTriggered`, `indexUpdated`, `zonesChanged`).
- `IFeatureModule::GetIcon` returns a **cached** HICON owned by the module — callers must not `DestroyIcon` it.
- The Features DLL is pure Win32 + Direct2D with `WIN32_LEAN_AND_MEAN`/`NOMINMAX` in its pch; don't drag WinRT into it, and don't link the WinUI app into it.
- The WallPaper DLL has its **own** boundary: `desktopsticker::IWallPaperModule` (`DesktopSticker.WallPaper/include/desktopsticker/IWallPaperModule.h`) + `CreateWallPaperModule` / `DestroyWallPaperModule`. It deliberately does **not** reuse `IFeatureModule`. The EXE loads it as a second, optional module via `Host::LoadWallPaper()`; failure must degrade to "wallpaper unavailable" and never affect zones/search/clock.
- `WallPaperEvents` callbacks may fire on a **worker thread** — the EXE marshals to the UI thread with `DispatcherQueue` before touching XAML.
- WallPaper links its own graphics stack (`d3d11 dxgi d2d1 dcomp dwmapi mfplat mfreadwrite mf mfuuid`). Keep it out of the Features DLL.
- The ResMon DLL has its own boundary too: `desktopsticker::IResMonModule` + `CreateResMonModule` / `DestroyResMonModule`, loaded as a **third** optional module. Its `Init` returns false when the WebView2 environment can't be created — the EXE keeps the instance and greys out the tray entry (`Available()`), it does not treat that as a load failure.
- WebView2 method-to-interface gotchas (verified against the pinned SDK): `put_IsWebMessageEnabled` is on `ICoreWebView2Settings` (not `ICoreWebView2`); `SetVirtualHostNameToFolderMapping` is on `ICoreWebView2_3` (QueryInterface); the args method is `TryGetWebMessageAsString` (it fails for non-string messages), and `get_WebMessageAsJson` **wraps a JS-sent string in another layer of quotes** — so read string messages with `TryGetWebMessageAsString` first.
- WebView2 async completions are delivered through the calling thread's message loop: wait for them by **pumping messages**, never by blocking.
- Threading: the UI thread owns all windows and layout state; the hotkey-hook, directory-watch, and weather threads talk to it only via posted messages or locked snapshots.

## Manual UI verification (tools/)

Behavior here can't be unit-tested, so verify by hand with these scripts (they're the reference harness; no build integration):
- `close_app.ps1` — graceful shutdown (WM_CLOSE to top-level windows, then WM_QUIT so `Shutdown` restores desktop icons). Also invoked by `build.bat`.
- `window_visibility.ps1` — list the process's top-level windows + visibility (use after embedding/restore changes).
- `panel_check.ps1` — `WindowFromPoint` hit-testing, to confirm which window actually receives clicks.
- `dblclick_test.ps1` / `wheel_test.ps1` / `space_test.ps1` — synthesize double-click, wheel, and double-space hotkey input.
- `screen_capture.ps1` / `capture_clock.ps1` — screenshot and crop the zone-card area / clock region for inspection.

## Fragile areas / known gotchas

- **Desktop shell embedding**: zones and the clock dock to the desktop via `Progman`/`WorkerW`/`SHELLDLL_DefView` (Wallpaper-Engine-style, undocumented); on failure the code degrades to a bottom-most normal window. Changes here must keep both paths working.
- **Zone window mouse input** has regressed repeatedly (see git log): making zone windows transparent-for-hit-test breaks clicks; layered/child window positioning and the low-level hook forwarding of button events are load-bearing. Test dragging, wheel scrolling, and click-to-open by hand after touching `ZoneWindow*`/`DesktopWorkspace`/`DropTarget`.
- **Layered surfaces + `SetParent`**: reparenting (embedding) resets the layered/ULW surface, so windows must be force-repainted after embedding. The clock must stay excluded from the "double-click blank desktop" detection.
- **Clock widget** is a click-through ULW child window and loads its PNG icons through WIC with a hand-drawn fallback; keep the fallback working (missing `assets/weather/S2` must not crash).
- **Weather/location is network-bound**: IP geolocation is deliberately fetched over a direct connection (bypassing the system proxy) to get the real location, falling back to the proxy path; weather comes from Open-Meteo via WinINet with a ~30 min background refresh and fast retry. Don't make these calls on the UI thread.
- **Config files** live in `%APPDATA%\DesktopSticker\` (`config.json`, `apps.json`, `layout.json`, `debug.log`, 5 MB rotation) and must tolerate being missing/corrupt (auto-rebuild, no crash).
- **WinUI sub-window lifecycle**: clicking the title bar X (or any `WM_CLOSE`) **destroys** the XAML `Window` object — it is not "hide". Every secondary window (`SettingsController`, `LauncherController`) must subscribe `Closed`, set a `closed_` flag, and rebuild the whole window on the next `Show`; calling `Show`/`AppWindow()` on a destroyed window stalls and throws (crash via tray subclass proc). `Show`/`Hide` must be try/caught because tray menu paths run inside native window procedures.
- Zones must also survive monitor/resolution changes; tray "Exit" must restore all native desktop icons and leave no stray zone windows.
- App is single-instance; hotkey is double-Space (configurable to `Alt+Space`) and must not fire while a text input has focus.
- **Wallpaper desktop embedding**: the wallpaper is a child of the wallpaper-host `WorkerW` (the one *after* the `SHELLDLL_DefView` WorkerW) held at `HWND_BOTTOM`, so it sits below desktop icons and below zone cards. `DesktopHost::FindWallpaperWorkerW` is a **self-contained reimplementation** — it deliberately does not touch Features' load-bearing `DesktopShellIntegration`; on failure the window degrades to bottom-most. `WallPaperWindow::ReassertBottom()` must be re-called after zone z-order changes.
- **Wallpaper must be excluded from the "double-click blank desktop" detection**, and its window is `WS_EX_NOACTIVATE` + `HTTRANSPARENT` so it never steals clicks. Do not add `WS_EX_TRANSPARENT` (transparent hit-testing has repeatedly broken zone input).
- **Wallpaper storage placement**: library root is chosen once (largest free **fixed** drive — removable/network/optical are excluded so an unplugged USB drive can't be picked) and then pinned; startup re-verifies volume serial + root directory file ID so a reused drive letter fails closed. It never auto-migrates; "change location" copies, verifies and keeps the old copy.
- **FFmpeg is dynamically loaded only** (`LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, never static-linked, no codec registered in Windows). `ffmpeg.exe` is invoked with an explicit argument array via `CreateProcessW` (no shell) and a timeout. Never write a transcode output over its input.
- **Resource manager is read-only and must stay so**: it may only stat files and enumerate processes/threads/modules — never create, modify or delete anything. Storage scans and CPU/memory sampling run on worker threads; the storage scan in particular must not block the UI thread and only the newest in-flight request's result is delivered.
- **The memory page only lists our own modules** (`is_own_module` in `resmon/Classify.h`): our exe / three feature DLLs, plus the bundled ffmpeg decode libraries — and the ffmpeg ones must actually resolve under `<exeDir>\ffmpeg\`, so a same-named library from System32 is never mistaken for our payload. System public DLLs, GPU drivers and third-party frameworks (including the self-contained Windows App SDK / DirectML / onnxruntime DLLs that live in our own directory) are excluded by design. Module ownership is decided from the **full path** (`GetModuleFileNameW`), never the base name.
- **ResMon uses a plain Win32 window on purpose** (not WinUI), to avoid the XAML sub-window lifecycle traps documented above. `WebView2Loader.dll` must sit next to the EXE — the NuGet targets only copy it into the ResMon project's OutDir, so the EXE's `PostBuildEvent` copies it again.
- **Thread names are load-bearing for the CPU tab**: `SetThreadDescription` is called at each thread's entry point (UI thread in `App.xaml.cpp`; the six worker threads in Features/WallPaper). If you add a thread, name it, or it shows as `线程 <tid>`.
- **`layout.json` is versioned** (currently 7 — quad-column mirroring, per-column card count, dynamic card heights). Any change to the persisted shape needs a bumped version plus an auto-migration path; loaders must keep accepting old layouts without crashing.

## Conventions

- C++20, precompiled headers (`pch.h`) per project; new files must be added to the `.vcxproj` (and `.filters`) — there's no globbing.
- Commit messages: lowercase English `feat:`/`fix:` one-liners summarizing behavior (match existing style).
- Comments/UI strings in Chinese are fine; keep identifiers in English.
- In the WallPaper project, put strategy/decision logic in **header-only pure functions** under `include/desktopsticker/wallpaper/` (no OS, no D3D) and keep OS/subprocess/GPU work in `src/`. That is what makes the logic unit-testable with `dtest` and is why the test count can grow without a GPU.
- New `.ps1` files containing non-ASCII must be saved **with a UTF-8 BOM**; PowerShell reads BOM-less scripts as GBK and fails to parse Chinese comments.
