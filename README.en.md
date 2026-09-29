# Desktop Sticker

A desktop organizer for Windows 11: it automatically sorts cluttered desktop icons into freely draggable **tile cards**, and ships a **double-space search launcher**, a **desktop clock widget** (time / sunrise-sunset / live weather), **animated desktop wallpapers**, and a **read-only resource monitor**. WinUI 3 front end + Win32/Direct2D feature layer, unpackaged and self-contained — no runtime installation required.

> This document describes the software itself. Build, run, test, project layout and architecture are covered in **[DEVELOPMENT.md](DEVELOPMENT.md)**; 中文版见 **[README.md](README.md)**。

## Key Features

### Desktop Zone Organizer

- **Auto-categorization**: on first launch, desktop icons are grouped by keyword rules into ten zones — Apps / Dev Tools / Office / Browsers / Media / Utilities / Social / Games / Folders / Other; files later dropped on the desktop are archived automatically by a directory watcher.
- **Four-column layout**: two columns docked to each side, card heights filling down to the taskbar; the number of cards per column (4 or 5) can be switched in Settings and relays out immediately.
- **Card interaction**: drag any empty area of a card to move it, drag an edge to resize (position/size auto-saved); double-click the title to collapse/expand; scroll wheel for smooth paging through more tiles.
- **Tile interaction**: double-click to open; drag a tile to another zone to move it, or onto empty desktop to restore it as a native icon; right-click menu supports open / remove.
- **Zone management**: right-click the title to rename / delete a zone; double-click empty desktop to toggle **clean desktop** (hides tiles and native icons, double-click again to restore).
- **One-click restore**: the tray "Restore Desktop" puts every collected icon back to its original position and re-shows it.

### Search Launcher

- Invoke with **double-space** (also configurable to `Alt+Space` or another combo in Settings); `Esc` closes.
- Search scope: desktop files, Documents/Downloads/Pictures/Videos/Music, Start Menu apps, and manually added apps.
- **Pinyin-initial** matching (e.g. `wx` → 微信), arrow-key navigation, Enter to open.
- Results grouped by source; icons loaded asynchronously and cached.

### Desktop Clock Widget

- A floating card centered at the top: an **analog dial** (smooth sweeping second hand) + a large digital `HH:MM:SS` + Country·Province·City location.
- **Sunrise/sunset**: a Sun–Earth orbit animation where the Earth's position equals the fraction of the day elapsed (sunrise at the left end, noon at the top, sunset at the right end, night along the dark arc), computed from real local sunrise/sunset times.
- **Current-hour weather**: temperature, condition, wind direction/speed, with colored icons ([QWeather S2](https://github.com/qwd/WeatherIcon) icon set).
- Data sources: IP geolocation (**direct connection for the real IP, bypassing the proxy**) + the free [Open-Meteo](https://open-meteo.com) API — no API key needed; refreshed in the background every 30 minutes with fast retry on failure.

### Animated Desktop Wallpapers

- Four wallpaper sources, all played **beneath the native desktop icons and zone cards**, without affecting zone collection or interaction:
  - **Video**: common formats (MP4 / WebM / MOV, …), looping, with 0.25×–4× speed control and an audio toggle (incl. volume).
  - **Animated image / image sequence**: GIF, animated WebP, or a folder of images (played in natural order, 100 ms/frame by default).
  - **Web**: HTML/CSS/JS pages (run in a WebView2 sandbox; transparent areas reveal the original desktop wallpaper).
  - **3D / shader**: presented directly via a self-owned Vulkan swapchain, with two built-in scenes — a **fullscreen shader** (plasma) and a **3D solid** (32-face truncated icosahedron, per-face coloring + orbiting camera + single-light Blinn-Phong), switched with `{"scene":"model"|"fullscreen"}` in `shader/scene.json`; both run at 60 fps.
- **Scene wallpapers**: a bundled Godot editor fork lets you author 2D/3D wallpaper scenes and export them as `.dswall` packages, rendered in-process by an embedded Godot runtime — users don't need to install Godot.
- **Dual decode paths**: system decoders (Media Foundation) first; when the system can't decode a source, it automatically falls back to the bundled FFmpeg shared libraries for real-time decoding; animated images deliberately prefer FFmpeg (MF only yields the first frame).
- **Decode at display size**: a 4K source on a 1080p screen only converts screen-sized pixels, instead of converting the extra resolution and throwing it away.
- **Performance variants**: transcodes "balanced" and "power-saver" variants in the background using an unmodified `ffmpeg.exe`, for high-bitrate sources or lower-end machines; variants can be rebuilt or deleted at any time, and **the source file is always left byte-for-byte unchanged**.
- **Auto-pause**: pauses when a fullscreen app covers the screen, stops on lock / display-off, supports manual pause, and resumes automatically when you return to the desktop.
- **Media library**: import files or folders (type auto-detected), thumbnails, list switching, rename, delete.
- **Storage location**: on first enable it picks the **fixed drive** with the most free space (`<drive>:\DesktopSticker\Wallpaper\`), recording the volume serial and root-directory file ID so a reused drive letter can't cause a wrong-volume write.

### Resource Monitor

A read-only resource viewer built on **Microsoft WebView2** (a separate window, opened from the tray menu), with three tabs:

- **CPU**: total process usage + **per-thread** breakdown (readable thread names: UI/zone rendering, hotkey hook, directory watch, wallpaper render & frame scheduling, pause monitor, weather background, transcode worker), plus child processes listed separately (the `ffmpeg.exe` during transcoding); refreshed every 2 seconds.
- **Memory**: working set / private commit / peak working set + the **program's own modules** (first-party code + the bundled ffmpeg decode libraries, by image size descending). **Excludes shared system DLLs and third-party frameworks.**
- **Storage**: total program footprint + a category breakdown bar + details + "Recalculate". Categories: wallpaper media library / FFmpeg payload / weather icon assets / debug symbols / program body / runtimes & frameworks / config & layout / logs / other.

**Read-only**: it never creates, modifies or deletes any file; scans run on worker threads and never block the UI.

### Settings

A Windows 11 Settings-style UI (Mica background cards, follows the system light/dark theme), opened from the tray menu:

- Search scope (desktop / known folders / Start Menu apps / hidden files);
- Wake-up hotkey (double-space / `Alt+Space`);
- Tiles: cards per column, column spacing, row spacing;
- Desktop clock toggle;
- Animated wallpapers: enable toggle, wallpaper list with thumbnails (typed labels), variant (original/balanced/power-saver), playback speed, audio toggle & volume, fullscreen/lock auto-pause, manual pause, import file / import folder, change storage location, open wallpaper folder; controls that don't apply to the current wallpaper type are greyed out;
- Manually add / remove apps;
- One-click restore of desktop icons.

## Component Libraries & Licenses

The license for Desktop Sticker's original code is independent of the third-party components below, which continue to apply under their own licenses. The full notices are in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

- **Godot Engine 4.7.2 (MIT)** — the scene-wallpaper editor and runtime library are based on a source fork of Godot (vendored under `tools/godot-src/`). The editor is used to author wallpaper scenes; the runtime library is loaded dynamically and used in-process by the wallpaper module. License copy: `third_party/GODOT-LICENSE.txt`.
- **FFmpeg (GNU LGPL v3)** — animated wallpapers use **unmodified** FFmpeg in two ways: running `ffmpeg.exe` as a subprocess to generate performance variants, and dynamically loading the `avformat` / `avcodec` / `avutil` / `swscale` shared libraries through their public C API as a decode fallback. **No FFmpeg library is statically linked, and no system decoder is registered on Windows.** The payload is a pinned BtbN Windows x64 LGPL shared build. License text: `third_party/LICENSE-FFmpeg.txt`.
- **OpenH264 (BSD)** — the pinned FFmpeg build integrates the Cisco OpenH264 encoder. This is the OpenH264 integrated into a third-party FFmpeg build, **not** Cisco's official prebuilt binary. License copy: `third_party/OpenH264-LICENSE.txt`.
- **MotionWallpaper (MIT)** — the animated-wallpaper component's approach is partly referenced from and ported from [MotionWallpaper](https://github.com/1114656/MotionWallpaper). License copy: `third_party/MotionWallpaper-MIT.txt`.
- **nlohmann/json (MIT)** — `third_party/nlohmann/json.hpp`, JSON parsing.
- **Microsoft WebView2 Runtime** — required by the resource monitor and web wallpapers (usually preinstalled on Windows 11).
- **Vulkan runtime** — required by 3D/shader wallpapers; the system `vulkan-1.dll` is loaded dynamically at runtime. This program **does not link or install** the Vulkan SDK.
- **Weather icons (CC BY 4.0)** — the icons under `assets/weather/S2/` come from [qwd/WeatherIcon](https://github.com/qwd/WeatherIcon) (QWeather), distributed as separate assets under CC BY 4.0; license text in `assets/weather/LICENSE-CC-BY-4.0.txt`.
