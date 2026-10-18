# AGENTS.md

Windows 11 desktop organizer: groups desktop icons into movable zone cards, plus a double-space search launcher and a desktop clock widget (analog dial / sunrise-sunset orbit / live weather). WinUI 3 (C++/WinRT) tray app + Win32/Direct2D feature DLL. README, code comments, and UI strings are in Chinese.

## Layout (note the nested same-name directories)

- `Desktop Sticker/Desktop Sticker.sln` — solution (one level down from repo root; paths in docs are absolute and include the nested dir twice).
- `Desktop Sticker/Desktop Sticker/` — WinUI 3 EXE project: `App`, `MainWindow`, `LauncherController`, `SettingsController`, `Host`. `Host` loads the Features DLL at runtime via `LoadLibrary`. `assets/weather/S2/` holds the QWeather icon set (61 PNGs, CC BY 4.0, license text next to it).
- `Desktop Sticker/DesktopSticker.Features/` — plain C++20 Win32 DLL (Direct2D/DirectWrite rendering, no WinRT/XAML in its pch): zones, desktop icon management, search index, pinyin, hotkey, config. `src/widgets/` has the clock + weather component.
- `Desktop Sticker/DesktopSticker.Tests/` — unit tests using the homegrown framework in `test_framework.h` (`namespace dtest`, no gtest).
- `Desktop Sticker/DesktopSticker.WallPaper/` — dynamic wallpaper DLL: D3D11 + DXGI + DirectComposition presentation, MF-primary decode with an FFmpeg shared-library fallback, media library, storage placement. Five backends behind `IWallpaperBackend` (`src/WallpaperBackend.h`): video/animated-image (`VideoBackend`), image sequence (`ImageSequenceBackend`, WIC), web (`WebBackend`, WebView2 **visual hosting**), 3D/shader (`VulkanBackend`, dynamic Vulkan loader) with two built-in scenes — a full-screen plasma shader and a procedurally built 32-face solid with an auto-orbiting camera and one-light Blinn-Phong — and the **scene wallpaper** (`GodotEmbeddedBackend` + `GodotEngineHost`: an embedded Godot runtime, `.dswall` packages only). No glTF/OBJ loading, particles or runtime GLSL compilation yet. `DecodeTarget.h` decides the decode output size (never bigger than the window). Strategy and geometry logic lives in **header-only pure functions** under `include/desktopsticker/wallpaper/` (`DecodeTarget`, `FrameAdvance`, `BackendKind`, `WallPaperPackage` — manifest parse/validate, `Polyhedron` — 60-vertex truncated icosahedron + a generic convex hull, `MatMath` — column-major matrices) so `dtest` can unit-test it without D3D/MF/Vulkan; OS adapters (drive enumeration, fullscreen detection, subprocess) live in `src/`.
- `Desktop Sticker/DesktopSticker.ResMon/` — read-only resource manager DLL (WebView2 hosted in a plain Win32 window): per-thread CPU, per-module memory, ordered storage classification. Formatting/classification/cpu-math/JSON assembly are **header-only pure functions** under `include/desktopsticker/resmon/`.
- `Desktop Sticker/Desktop Sticker/resmon/` — the resource manager frontend: plain `index.html` / `style.css` / `app.js`, **no framework and no build step**; xcopied next to the EXE and served via the WebView2 virtual host `resmon.local`.
- `Desktop Sticker/DesktopSticker.WallpaperStudio/` — `dstk-studio.exe`, the 2D wallpaper editor host: a thin libgodot driver (`LoadLibraryExW` the scene-wallpaper runtime, pass `--path <exeDir>\studio`, pump `dstk_godot_iteration`). It contains **no editing logic** — the editor is a Godot project. `--self-test` loads the engine and runs a few frames for smoke builds.
- `tools/wallpaper-studio/` — the editor's Godot project (`project/src/{doc,build,export,ui,cli}` + `tests/`), the AES pack signing key (`signing_key.txt`, the single source of truth — never regenerate it), the key/pack helper scripts, and `run_tests.ps1` (headless test runner). The studio vcxproj's `PostBuildEvent` xcopies `project/` to `<exeDir>\studio\` and the key inside it.
- `Desktop Sticker/Desktop Sticker (Package)/` — MSIX packaging project; the app actually runs unpackaged/self-contained, don't rely on package identity.
- `third_party/nlohmann/json.hpp` — only third-party dependency (include root is `third_party/`). `third_party/` also holds the FFmpeg/OpenH264/MotionWallpaper notices and the Godot engine's MIT license (`GODOT-LICENSE.txt`).
- `tools/*.ps1` — PowerShell UI harness for manual verification (see below). Payload scripts: `tools/prepare_ffmpeg.ps1` (FFmpeg), `tools/prepare_vulkan.ps1` (Vulkan-Headers + Vulkan-Hpp), `tools/prepare_shaderc.ps1` (builds `glslangValidator.exe` into `tools/shaderc/`). All three write to gitignored dirs.
- `tools/godot-src/` — **vendored** Godot 4.7.2 source: the scene-wallpaper engine fork (tracked in git, ~14k files). Build it with `tools/build_godot.ps1`; `tools/prepare_godot_src.ps1` only re-fetches a pristine copy from the pinned archive (disaster recovery, not part of normal builds).
- `tools/godot/` — built Godot **runtime payload** for the scene-wallpaper backend (`godot.windows.template_release.x86_64.dll` + license; collected by `build_godot.ps1` and copied next to the EXE by its `PostBuildEvent`; gitignored).
- `tools/godot-wallpaper-template/` — the scene-wallpaper editor template (Godot project + `addons/dstk_wallpaper/` plugin: scene validation and `.dswall` export).
- `tools/godot-host-test/` — standalone host harness for the runtime library (loads a project/`.dswall` into a `WorkerW` child window; `build.ps1`; `--project/--pack/--dswall/--top/--auto-exit`). M1/M2 acceptance tool.
- `docs/acceptance.md` — manual acceptance checklist; read before/after touching zone or launcher behavior. `docs/superpowers/` holds the original plan/spec.
- `THIRD_PARTY_NOTICES.md` — third-party disclosure (FFmpeg LGPL v3, OpenH264 BSD, MotionWallpaper MIT).

## Build & test (Release x64 only)

一键编译：仓库根目录 `build.bat`（双击或 `build.bat test`，含还原、编译、关停运行中的实例、可选跑测试）。

日常迭代用 `tools/dev_build.ps1`（差量编译 + DLL 同步 + 跑测试，只重编有改动的项目）；改动涉及头文件、
`.vcxproj`，或怀疑 `obj/` 脏了（LNK 怪错、测试段错误但代码看着没错）时改用 `tools/rebuild_and_test.ps1`
（全量 `-t:Rebuild`）。两者都带 `-Filter <子串>` / `-List` / `-NoTest` / `-SkipClose`。

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
./Tests/DesktopSticker.Tests.exe        # expect: 259 passed, 0 failed
```

Vulkan headers + shader compiler (only needed to build/test ④, not to build the app):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_vulkan.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_shaderc.ps1
```

The first clones Vulkan-Headers/Vulkan-Hpp at pinned commits; the second CMake-builds glslang's `StandAlone` **out-of-source** (into `tools/shaderc/build`, so a glslang checkout elsewhere is never polluted). Both are optional: without them `VulkanBackend.cpp` compiles its `__has_include` fallback and ④ reports unavailable.

FFmpeg payload (dynamic wallpaper's decoder fallback + transcode backend):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_ffmpeg.ps1
```

Downloads the pinned BtbN LGPL shared build into `tools/ffmpeg/` (runtime) and extracts its
headers into `tools/ffmpeg-sdk/` (build-time, for `decltype(&av_*)` declarations). Both are
gitignored and SHA-256 verified. **The build still succeeds without them** — the FFmpeg path is
gated by `__has_include` and degrades to "fallback decoder unavailable". PowerShell reads `.ps1`
as GBK unless the file has a UTF-8 BOM, so keep the BOM on scripts containing Chinese comments.

Godot fork (scene wallpapers; only needed when changing the engine, editor or runtime library):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1                 # runtime DLL -> bin + tools/godot
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1 -Target Editor # editor (for authoring/export)
```

First-time Godot builds need SCons (`py -3 -m pip install scons`) plus the engine's build deps
(`misc\scripts\install_d3d12_sdk_windows.py`, `install_angle.py`, `install_accesskit.py`). All
render drivers stay enabled (Vulkan + D3D12 + OpenGL/ANGLE) and AccessKit is built in; the runtime
target is `library_type=shared_library` with `disable_path_overrides=no` (the host passes
`--main-pack`). Without the DLL the app still builds and runs — scene wallpapers just report
unavailable.

Output paths are split (both gitignored):
- EXE → `Desktop Sticker/x64/Release/Desktop Sticker/Desktop_Sticker.exe`
- Features DLL + tests → `Desktop Sticker/bin/x64/Release/` (and `.../Release/Tests/`)

Wallpaper editor (`dstk-studio.exe`) — its UI/document/export logic is GDScript, so MSBuild only builds the host EXE and copies the project:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File build.bat                     # EXE + xcopy project -> <exeDir>\studio\
"D:/project/Desktop Sticker/Desktop Sticker/x64/Release/Desktop Sticker/dstk-studio.exe"   # run the editor
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1   # headless GDScript tests (needs the build)
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1 -Only <case>   # one case
```

`tools/dev_build.ps1` / `tools/rebuild_and_test.ps1` run those tests as step `[5/5]`; `-NoStudio` skips them. Edit `tools/wallpaper-studio/project/**` (the tracked source), never `<exeDir>\studio\**` — a rebuild overwrites it.

Gotchas:
- The Debug test exe crashes on this machine (Debug CRT environment issue) — always test in Release.
- `DesktopSticker.Features.dll` and `DesktopSticker.WallPaper.dll` must sit next to the EXE and next to the tests exe. The EXE's `PostBuildEvent` copies them from `bin\$(Platform)\$(Configuration)\` to OutDir, and `build.bat test` copies the Features one into `Tests\`.
- The same PostBuildEvent also xcopies `assets\weather\`, `tools\ffmpeg\` and `tools\godot\` next to the EXE; if weather icons, the wallpaper fallback or the scene-wallpaper runtime vanish at runtime it's this step, not the code.
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
- **Scene wallpapers (`.dswall`, reusing `BackendKind::Shader3D`) run the Godot engine in-process**: `GodotEmbeddedBackend` drives `GodotEngineHost`, which `LoadLibraryExW`s `<exeDir>\godot\godot.windows.template_release.x86_64.dll` (same "never import-link a payload" discipline as FFmpeg) and calls the fork's `dstk_*` exports. libgodot is a **process-wide singleton** (the engine cannot be destroyed/re-created), so `Close()` only pauses/hides; per-pack switching goes through `dstk_godot_load_pack` (mount the new PCK + change the main scene). The engine renders into its own `WorkerW` child window (created hidden, adopted **before its first show** — the AccessKit adapter requires that order — and kept for the process lifetime), not into `WallPaperWindow`.
- `.dswall` import (ZIP: `project.pck` + `manifest.json` + `poster.png`) is unpacked by `MediaLibrary` with a `tar.exe` subprocess into `media/<id>/shader/`, and the poster moves to the item root (the existing thumbnail path); manifest parsing/validation is the header-only pure `WallPaperPackage.h` (dtest-covered). Mapping `.dswall` to `Shader3D` keeps `BackendKind`/`Types.h`/`library.json` untouched (no ABI or migration change).
- The ResMon DLL has its own boundary too: `desktopsticker::IResMonModule` + `CreateResMonModule` / `DestroyResMonModule`, loaded as a **third** optional module. Its `Init` returns false when the WebView2 environment can't be created — the EXE keeps the instance and greys out the tray entry (`Available()`), it does not treat that as a load failure.
- WebView2 method-to-interface gotchas (verified against the pinned SDK): `put_IsWebMessageEnabled` is on `ICoreWebView2Settings` (not `ICoreWebView2`); `SetVirtualHostNameToFolderMapping` is on `ICoreWebView2_3` (QueryInterface); the args method is `TryGetWebMessageAsString` (it fails for non-string messages), and `get_WebMessageAsJson` **wraps a JS-sent string in another layer of quotes** — so read string messages with `TryGetWebMessageAsString` first.
- WebView2 async completions are delivered through the calling thread's message loop: wait for them by **pumping messages**, never by blocking.
- **The WallPaper DLL's Web backend uses WebView2 *visual hosting*, not a hosted child window** (`CreateCoreWebView2CompositionController` + `put_RootVisualTarget`, then `D3dContext::SetRootVisual`). Windowed hosting *does not composite* when the host window lives inside the desktop `WorkerW` — environment/controller/navigation/window-title/window-tree/visibility/size all succeed and not a single pixel appears (verified with a solid-red probe page); the same window moved to top-level renders fine. So: don't "fix" this by adding a host HWND, and don't drop the `setRootVisual` / `restoreRootVisual` handoff in `BackendContext` — that handoff is what makes web wallpapers visible. Also note `ICoreWebView2CompositionController` exposes no `get_CoreWebView2`; QueryInterface it for `ICoreWebView2Controller` first.
- The WallPaper project links the WebView2 loader **statically** (`WebView2LoaderPreference=Static` in its vcxproj, plus the NuGet `Microsoft.Web.WebView2.1.0.3719.77` include dir under `Desktop Sticker/packages/`). Deliberate: the DLL is loaded by the host with `LoadLibrary`, so an import dependency on `WebView2Loader.dll` would turn "that file is missing" into "the whole wallpaper module is unavailable", while a missing WebView2 *runtime* must only disable ③.
- **④ Vulkan backend**: entry points are resolved from the system `vulkan-1.dll` (`VULKAN_HPP_DEFAULT_DISPATCHER`, no import library, no Vulkan SDK). Two null-function-pointer traps cost a hard crash each before the spike ran (process exits silently, event log says only "unknown module, offset 0"): the surface extensions **must** be enabled on the instance (`VK_KHR_surface` + `VK_KHR_win32_surface`) or `vkCreateWin32SurfaceKHR` is never loaded, and `VULKAN_HPP_DEFAULT_DISPATCHER.init(*device)` **must** be called after device creation or every device-level entry is null. `Impl::init_seh` wraps init in `__try/__except` so a driver/dispatcher fault degrades to "④ unavailable" instead of taking down the wallpaper module — note a function may not mix `__try` with C++ `try/catch` (C2713), hence the SEH wrapper only forwards and `Open()` keeps the C++ handler. `DSTK_VULKAN_VALIDATION=1` opts into the validation layer when it is installed.
- **④ has two built-in scenes, picked by the item's `shader/` directory**: a `.frag` present -> the full-screen shader; otherwise -> the built-in 32-face solid (truncated icosahedron, auto-orbiting camera, one directional light + Blinn-Phong). `scene.json` with `{"scene":"model"|"fullscreen"}` overrides the guess, and its presence is also what lets `classify_directory` accept a folder that has no shader source. The solid is generated in `Polyhedron.h` (the reference Three.js page used ConvexGeometry; here it's our own generic convex hull, so `dtest` can assert 32 faces / 12 pentagons / 20 hexagons / V-E+F=2). Camera framing is a real trap: with fovY 0.62 and model radius 2 the eye must sit at distance >= ~9 or the camera ends up *inside* the solid.
- **Column-major matrix layout is `m[col * 4 + row]`** (`MatMath.h`). Reading it as row-major silently mirrors the scene's rotation, so the rotations are pinned by tests on the right-hand-rule outcome (+X -> -Z for +90 deg about Y). Note the perspective matrix targets **Vulkan's** clip space: depth [0,1] and a negated y.
- **`FrameSchedulerLoop::wait_ticks` takes 100-ns units, not milliseconds.** Writing `1600000` "for 16 ms" actually waits 160 ms and pins self-presenting backends (web, shader) to ~6 fps while every other counter looks healthy. The per-backend diag lines exist to catch exactly this.
- `IVideoSource` takes a `VideoSourceOptions` (max output size + `preferFfmpeg`). Video is decoded at **display size**, not source size — a 4K source on a 1080p screen was spending ~25ms/frame converting pixels that D2D then threw away (`DecodeTarget.h`); the same option makes animated images prefer FFmpeg, because MF's WIC source only yields the first frame of a GIF/WebP.
- **Video hard decode is FFmpeg's CUDA hwaccel (`-hwaccel cuda` as an API call), not a Vulkan path and not `h264_cuvid`.** A deliberate decision: on an RTX 4060 with H.264 the shortest chain (hwaccel decode -> `scale_cuda` -> `hwdownload` -> sws) has the least latency and the fewest sync hazards; Vulkan Video / `AVVkFrame` was evaluated and rejected as over-engineering. Five traps, each verified on the bundled build with `tools/ffmpeg/ffmpeg.exe` (the whole chain runs ~175 fps = 5.7 ms/frame on the real 4K60 test clip, so **60 fps needs no extra threads** — the stutter was us taking the slow branch):
  1. `codec_ctx->hw_device_ctx` alone does **nothing** — you must also install `codec_ctx->get_format` returning `AV_PIX_FMT_CUDA` when it is offered. Without it the decoder hands back system-memory frames and every later stage pays for it.
  2. Selecting the decoder **by name** (`h264_cuvid`) is **worse**, not safer: it always downloads, and its own log says `Formats: HW: nv12 | SW: nv12`. Only `-hwaccel cuda` over the generic decoder yields `pix_fmt: cuda`.
  3. `scale_cuda` scales fine in this build (no `--enable-libnpp` needed) **but can only output a yuv format** — `format=bgra`/`rgba` fails at `avfilter_graph_config`, and so does using it as a pure same-size conversion.
  4. `hwdownload`'s output must be pinned to the frame's `sw_format` (`format=nv12`); putting `format=bgra` right after it fails with `Invalid output format bgra for hwframe download`. The NV12->BGRA conversion is ours to do, and doing it with swscale at *display* size is ~4x cheaper than at source size.
  5. The `buffer` source recipe **flipped with the bundled FFmpeg 8.1** (`avfilter-11`, BtbN `n8.1.2`): the old "create via `avfilter_graph_create_filter` with string args, attach `hw_frames_ctx` afterwards" now fails at init with `Unspecified pixel format` + EINVAL — **every** string-args variant fails, including empty args, because 8.1 requires the pixel format at init time. The working order is three steps: `avfilter_graph_alloc_filter` (alloc, no init) → `av_buffersrc_parameters_set` (format=CUDA, width/height, time_base, sample_aspect_ratio **and** `hw_frames_ctx` together) → `avfilter_init_dict(ctx, nullptr)`. Verified by `tools/probe_ffmpeg_buffersrc.exe` (source in `tools/`), which also measures the chain end-to-end (3.5 ms/frame on the 4K60 clip). The old `pix_fmt`-in-args trap ("requires hw_frames_ctx to be non-NULL") was real on the pre-8.1 build — check `ffversion.h` before trusting either recipe.
  6. The filter graph pins the decoder's `hw_frames_ctx` pool. **After an EOF→seek loop the decoder may derive a new pool** and `av_buffersrc_add_frame_flags` then rejects every frame — the chain silently degrades to 4K download + sws (~15 ms/frame, 7 fps slideshow). Drop the graph on seek (`drop_cuda_graph`) and rebuild once if buffersrc rejects a frame after that.
  Because every one of these fails **silently**, the first decoded frame's real pixel format is logged once (`FFmpeg: first frame format=...`), every filter-graph failure logs FFmpeg's own error text (`ff_err()` via `av_strerror`), and `convert()` logs chain-hit statistics every 120 CUDA frames (`FFmpeg: cuda frames=... chain=... fallback=...`) — an `AVERROR` number in a log is unreadable and cost us a whole debugging round.
- **Do not decode the whole backlog in one tick.** `VideoBackend::ProduceFrame` used to pull `adv.consume` frames per tick with no cap; once a tick ran long, `consume` grew with `elapsedMs` and the loop locked onto a fixed point where it *decoded* ~90 frames/s but *presented* ~19 (4K60: decode 25-40 ms per tick, period 35-53 ms, forever). Consuming is now capped at `ceil(speed)+1` frames per tick and the remainder is absorbed by the render loop's deadline re-anchor. The proper fix is PTS-based frame dropping (`should_drop_to_catch_up` in `FrameSchedulerLoop.cpp` is still not wired).
- **A D3D11 device shared with a hardware decoder must be created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` and must have `ID3D11Multithread::SetMultithreadProtected(TRUE)` called on it** (`D3dContext.cpp`). Both MF's DXVA and FFmpeg's hardware transforms touch the immediate context from their own worker threads; without serialization the symptom is "plays a few frames, then blocks forever inside the frame-read call" (that is what the `mf-d3d11` path did for weeks). This exact pair was lost once when `create_device_and_swapchain` was rewritten and the hang came straight back — keep the flag with the device, not in a comment. `ID3D11Multithread` needs `<d3d11_4.h>`, which is in the WallPaper pch for this reason.
- **The NV12 full-screen triangle must be drawn with an explicit `CullNone` rasterizer state.** Its vertices `(-1,-1)→(3,-1)→(-1,3)` are counter-clockwise in NDC; the default rasterizer state (`RSSetState(nullptr)` = CULL BACK, CW=front) culls the whole triangle — `Draw(3)` returns fine, zero pixels land, and the back buffer stays black. Symptom: "frame rate is perfect, screen is black" on the `mf-d3d11` path while the D2D path (which doesn't use the triangle) shows video. Fix: `D3D11_RASTERIZER_DESC` with `CullMode = D3D11_CULL_NONE` (`nv12Rs_`).
- **`VideoBackend::ProduceFrame` caps backlog consumption per tick, and the cap depends on the clock master.** Decoding the whole `adv.consume` backlog locks the loop onto a fixed point where it decodes ~90 frames/s but presents ~19 (every decoded frame costs ~13 ms, so the tick stretches and the backlog never clears). With audio as master the cap is `ceil(speed)+1` (content must keep up with the sound — the audio clock only advances in ~10 ms mixing quanta, so ticks are inherently long and decoding just 1 frame/tick makes the video drift against the audio); muted (QPC master) the cap is `ceil(speed)` = 1 frame/tick = 1 present/tick, so the presented rate equals the tick rate (~53 fps on the 4K60 clip). The proper fix for both rates is PTS-based frame dropping (`should_drop_to_catch_up` in `ClockPolicy.h` is still not wired).
- Threading: the UI thread owns all windows and layout state; the hotkey-hook, directory-watch, and weather threads talk to it only via posted messages or locked snapshots.

## Manual UI verification (tools/)

Behavior here can't be unit-tested, so verify by hand with these scripts (they're the reference harness; no build integration):
- `close_app.ps1` — graceful shutdown (WM_CLOSE to top-level windows, then WM_QUIT so `Shutdown` restores desktop icons). Also invoked by `build.bat`.
- `window_visibility.ps1` — list the process's top-level windows + visibility (use after embedding/restore changes).
- `panel_check.ps1` — `WindowFromPoint` hit-testing, to confirm which window actually receives clicks.
- `dblclick_test.ps1` / `wheel_test.ps1` / `space_test.ps1` — synthesize double-click, wheel, and double-space hotkey input.
- `screen_capture.ps1` / `capture_clock.ps1` — screenshot and crop the zone-card area / clock region for inspection.
- `wallpaper_child_windows.ps1` — dump the wallpaper window's descendant tree (class / visibility / style / rect). The wallpaper window is a child of `WorkerW`, so `EnumWindows` won't find it; this walks top-level windows' descendants instead. This is how the "WebView2 is created and visible but nothing composites" finding above was established.

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
- **Scene wallpaper ↔ Vulkan switching on this machine's NVIDIA driver**: `vkDestroy*` teardown intermittently AVs/hangs inside `nvoglv64` (observed before the scene feature existed; two crashes at the same offset). `VulkanBackend::Close()` therefore **parks** the device and the next open **takes it back** and rebuilds only the scene resources (`Impl::reinit_scene`). Never destroy a Vulkan device in-process again without re-verifying switching, and never create a second swapchain on the same window — the second one does not display (that was the black screen). Expected log pair: `vulkan backend: device parked` → `vulkan backend opened (reused device)`.
- **Pause keeps the last frame**: the scheduler's paused branch must not `d3d_.Clear` while a backend exists (clearing turned manual pause black for video wallpapers); only "no backend" resets to the idle colour.
- **AccessKit's Windows subclassing adapter must be created before a window is first shown**, otherwise the Rust side panics and kills the process; the embedded engine registers it only while the host window is still hidden (`IsWindowVisible` guard in `display_server_windows.cpp`).
- **FFmpeg is dynamically loaded only** (`LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, never static-linked, no codec registered in Windows). `ffmpeg.exe` is invoked with an explicit argument array via `CreateProcessW` (no shell) and a timeout. Never write a transcode output over its input.
- **Resource manager is read-only and must stay so**: it may only stat files and enumerate processes/threads/modules — never create, modify or delete anything. Storage scans and CPU/memory sampling run on worker threads; the storage scan in particular must not block the UI thread and only the newest in-flight request's result is delivered.
- **The memory page only lists our own modules** (`is_own_module` in `resmon/Classify.h`): our exe / three feature DLLs, plus the bundled ffmpeg decode libraries — and the ffmpeg ones must actually resolve under `<exeDir>\ffmpeg\`, so a same-named library from System32 is never mistaken for our payload. System public DLLs, GPU drivers and third-party frameworks (including the self-contained Windows App SDK / DirectML / onnxruntime DLLs that live in our own directory) are excluded by design. Module ownership is decided from the **full path** (`GetModuleFileNameW`), never the base name.
- **ResMon uses a plain Win32 window on purpose** (not WinUI), to avoid the XAML sub-window lifecycle traps documented above. `WebView2Loader.dll` must sit next to the EXE — the NuGet targets only copy it into the ResMon project's OutDir, so the EXE's `PostBuildEvent` copies it again.
- **Thread names are load-bearing for the CPU tab**: `SetThreadDescription` is called at each thread's entry point (UI thread in `App.xaml.cpp`; the six worker threads in Features/WallPaper). If you add a thread, name it, or it shows as `线程 <tid>`.
- **`layout.json` is versioned** (currently 7 — quad-column mirroring, per-column card count, dynamic card heights). Any change to the persisted shape needs a bumped version plus an auto-migration path; loaders must keep accepting old layouts without crashing.
- **Wallpaper Studio must ship as a directory, never as a PCK**: exporting writes `main.scn` into `res://build/<pack_id>/` so `ResourceSaver` localizes the texture paths to `res://…`; inside a packed PCK `res://` is read-only and every export fails. The studio project also uses **no `class_name`** (a runtime-only build has no `.godot/global_script_class_cache.cfg`) — dependencies are `preload` consts — and **no `static var` in any preloaded script** (it corrupts the heap on shutdown while every test still passes).
- **The scene-package signing key is baked into libgodot**: `tools/build_godot.ps1` regenerates `core/script_encryption_key.gen.cpp` from `tools/wallpaper-studio/signing_key.txt`, so changing that file silently breaks every already-shipped runtime and every old `.dswall`. Encryption only stops casual unzip/inspection (the key is recoverable from the DLL, and a `.dswall` may carry GDScript with the full Godot API) — it is **not** a security boundary.
- **Studio scene nodes must use `set_anchors_and_offsets_preset`, not `set_anchors_preset`**: `set_anchors_preset` keeps a control's current on-screen position, so a never-in-tree 1920×1080 root keeps `offset_right/bottom = 1920/1080` and, once added, layout computes `size = parent + offset` = double canvas — every text/image silently flies off the visible area (symptom: only a corner of one colour layer shows). Guarded by `builder_layers_fill_parent_without_doubling`.
- **`GPUParticles2D.visibility_rect` defaults to 200×200 around the origin** — particles outside it are culled, so a layer can look completely empty; `LayerNodes` sets a 4096×4096 rect and places the emitter at the canvas bottom centre (direction defaults upward). Particle textures are inlined into the baked scene, so a huge texture bloats the pack.
- **选区框（元素的位置与尺寸）以 `transform.w/h` 为准，场景里用锚点表达**：`src/doc/frame.gd` 是唯一几何真源（`of`/`materialize`/`anchors`/`resized_rect`/`anchor_point`/`hit_test`/`cursor_for`/`space_map`/`layer_map`/`scale_children`，纯函数、`tests/test_frame.gd` 钉住）；`SceneBuilder.apply_layer_node` 把它写成 `anchor_*` + offsets=0，所以显式框的图层在任意窗口尺寸下等比自适应，`w/h = 0` 才回到"铺满父级"的老语义。老的统一 `scale` 在加载时折算进 `w/h`（`Frame.migrate_scale`），编辑器不再暴露 `scale`。画布交互在 `src/ui/selection_overlay.gd`（铺满画布区的一层，自己画框/手柄、自己处理点击）：位移必须用 `get_global_mouse_position()` 的差值算（**不能用 `event.position`**：控件自己在动，局部坐标会跳）；拖动中调 `main.apply_live()` 只改节点不重建，松手才 `apply_change(true)`。点空白取消选择、点中哪层选哪层（`CanvasView.layer_at_screen`，递归进容器），Alt+点选最外层容器。
- **图层是树（`group` = 容器 / 画布）**：`doc.layers` 只是根数组，"某一层"一律用路径 `PackedInt32Array` 寻址（`main.sel`）。结构编辑全在 `doc.gd`：`walk`/`layer_at`/`insert_layer`/`remove_at`/`move_within`/`reparent`（保持屏幕位置）/`wrap_in_group`/`set_canvas`（按比例缩放所有元素）。容器里子层的坐标**相对容器的框**；容器改尺寸时子层按同一比例改写（`Frame.scale_children`，拖动时每帧从**快照**重算，别在上一帧结果上再乘）。`insert_layer`/`reparent` 千万不能用"子数组为空"判断父级不是容器——刚建的容器就是空的，那样会把新层静默加回根层（踩过）。
- **遮罩 = 着色器，不是位图**：`src/doc/mask.gd` 只算矩形与 UV 换算；`filters.gd` 的 `layer_shader()` 在画布 UV 比例上算 alpha（矩形/圆角/椭圆/图片 alpha，羽化 = smoothstep，反相 = 1-a）。好处：不栅格化（拖遮罩手柄不用重算几十万像素）、对文字同样有效、窗口放大不糊。场景里的挂法：色彩/暗角/色调分离/遮罩共用一个 `ShaderMaterial` 挂在**内容叶子**上（混合模式走 `render_mode`，避免和材质抢槽）；模糊另走两趟 `SubViewport`（可分离高斯，半径按图层宽高比例给），此时材质挂在最外层容器上（那层已在画布坐标）；容器自带遮罩/滤镜时整棵子树先渲进一个 `SubViewport` 再给容器挂材质，否则会覆盖子层自己的材质。阴影/描边/发光是"复制内容 + modulate 染色 + 垫在内容下面"（发光那份过一趟模糊），对粒子与容器不生效。
- **着色器 uniform 只认有限类型**：`set_shader_parameter` 收到 `Rect2` 会直接把进程打挂（访问冲突，无头下也一样）→ 几何一律压成 `Vector4`（`Filters._as_vec4`、`mask_rect`）。
- **`LayerNodes.apply_material` 必须包含 root 自己**：纯色/图片/文字的内容节点就是叶子，只遍历 children 会让整层的滤镜/遮罩静默失效（踩过）。
- **无头测试不编译着色器**（dummy 渲染器），遮罩/滤镜/模糊的语法错与 uniform 类型错只能靠 `tests/render_smoke.gd` 抓：真渲染器、离屏小窗跑 6 帧 —— `dstk-studio.exe --script res://tests/render_smoke.gd --position 3000,3000 --resolution 320x180`，判据是输出有 `PROBE RENDER OK` 且没有 `SHADER ERROR`。名字不带 `test_` 前缀，所以不进无头测试集。另：SceneTree 脚本的入口是 `_initialize()`，没有 `_ready()`。
- **`run_tests.ps1` 同步用 `robocopy /MIR`**：删掉的 `tests/test_*.gd` 必须跟着从 `<exeDir>\studio\` 消失，否则无头测试跑的是上一次的残留副本（踩过：删掉临时探针后它还在跑）。
- **Studio 卡死的四处现场约定（别一项一项往回改）**：
  1. 宿主编译期就把第三方 Vulkan 隐式层关掉（`VK_LOADER_LAYERS_DISABLE=~implicit~` + `DISABLE_VULKAN_OBS_CAPTURE=1`，见 `main.cpp` 的 `shield_from_vulkan_overlays`）。本机注册表 `ImplicitLayers` 里有 OBS 的抓帧层，只有 `disable_environment`、没有 `enable_environment`，所以**恒生效**：OBS 没开，`dstk-studio` 里照样加载了 `C:\ProgramData\obs-studio-hook\graphics-hook64.dll`（hang dump 的 `lm` 可查）。这类层在渲染线程做 inline hook，是卡死/掉帧来源；编辑器与壁纸都不需要被抓帧。
  2. 对话框走内嵌子窗口（`window/subwindows/embed_subwindows=true`）。原生子窗口会给同一进程再建一条 Vulkan 交换链，本机 NVIDIA 驱动在这条路径上出现过"打开文件选择框后整窗未响应"；内嵌之后只有一个 `Engine` 窗口（`EnumWindows` 可验）。
  3. 宿主自带卡顿自捕获：主循环每帧记开始时间，另有线程盯着，**单帧超过 3 秒**就在 `%LOCALAPPDATA%\DesktopSticker\stalls\` 写 `stall_*.dmp`（dbghelp minidump，栈可完整走通）+ `stalls.log`。下次再卡，先看那里，不要再靠外部 5 秒看门狗（它只在窗口被系统标成"未响应"后才动，且进程一关证据就没了）。
  4. **4.7.2 的消息泵回归**（这一条最容易误判成"真卡死"）：`DisplayServerWindows::process_events()` 里过滤式 PeekMessage（PR #109639）让窗口闲置约 5 秒就被 Windows 判成 Not Responding 并幽灵化——画面静止、点击全被系统吞掉，但引擎每帧照跑、`SendMessageTimeout(WM_NULL)` 照应答。上游 issue #124276（4.7.1 无此问题）。修法：pump 前加一次**未过滤**的 `PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)`（不取走消息，不影响限流）。判据：空闲 14 秒 `IsHungAppWindow` 恒为 False。注意 4.8-dev 上同样改法无效。
  另外：`dstk_godot_destroy()`（`Main::cleanup()`）在退出路径上会让窗口"未响应"数秒并偶发 `RaiseFailFastException`，所以正式退出走 `TerminateProcess`（**不是** `ExitProcess`——后者会给每个 DLL 发 `DLL_PROCESS_DETACH`，libgodot 的 detach 照样会碰合成层并在 `CoreMessaging.dll` 里 fast-fail），不跑引擎收尾，stderr 也因此不会刷 `BUG: Unreferenced static string` / PagedAllocator 泄漏日志。

## Conventions

- C++20, precompiled headers (`pch.h`) per project; new files must be added to the `.vcxproj` (and `.filters`) — there's no globbing.
- Commit messages: lowercase English `feat:`/`fix:` one-liners summarizing behavior (match existing style).
- Comments/UI strings in Chinese are fine; keep identifiers in English.
- In the WallPaper project, put strategy/decision logic in **header-only pure functions** under `include/desktopsticker/wallpaper/` (no OS, no D3D) and keep OS/subprocess/GPU work in `src/`. That is what makes the logic unit-testable with `dtest` and is why the test count can grow without a GPU.
- In `tools/wallpaper-studio/project`, keep the same split: document/scene/export logic in `src/doc`, `src/build`, `src/export` (pure GDScript, headless-testable — add a `tests/test_*.gd` case when you change it) and UI in `src/ui`. UI files must not touch the built node tree directly; they go through `StudioDoc` + `main.apply_change()`.
- New `.ps1` files containing non-ASCII must be saved **with a UTF-8 BOM**; PowerShell reads BOM-less scripts as GBK and fails to parse Chinese comments.
