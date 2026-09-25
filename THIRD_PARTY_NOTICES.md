# 第三方软件声明

Desktop Sticker 原创源代码的许可与下列第三方组件相互独立。第三方组件继续适用各自的许可证条款。

## FFmpeg

性能副本后端调用未经修改的 FFmpeg 可执行文件；系统缺少解码器时，原画播放器动态加载同目录的 FFmpeg 共享库作为备用解码器。二者均来源于固定版本的 BtbN Windows x64 LGPL shared build，按照 GNU LGPL v3 授权。二进制负载在 `Tools/ffmpeg` 中保留对应许可证和声明，不向 Windows 注册系统解码器。

- 项目主页：https://ffmpeg.org/
- 构建分发：https://github.com/BtbN/FFmpeg-Builds
- 仓库内声明：`third_party/FFmpeg-NOTICE.txt`
- 许可正文：`third_party/LICENSE-FFmpeg.txt`
- 运行期负载位置：应用目录下的 `ffmpeg\`（由 `tools/prepare_ffmpeg.ps1` 获取，不入版本库）

Desktop Sticker 只以两种方式使用 FFmpeg：以子进程方式运行未经修改的 `ffmpeg.exe`；以及通过公开 C 接口动态加载未经修改的 `avformat` / `avcodec` / `avutil` / `swscale` 共享库。不静态链接任何 FFmpeg 库。

## OpenH264

固定的 BtbN FFmpeg 构建启用了 Cisco OpenH264 编码器。OpenH264 源代码采用 BSD 许可证；发布负载在 `Tools/ffmpeg/LICENSE-OpenH264.txt` 中保留其版权、条件和免责声明。

- 项目主页：https://github.com/cisco/openh264
- 仓库内许可证副本：`third_party/OpenH264-LICENSE.txt`

这里使用的是第三方 FFmpeg 构建中集成的 OpenH264，不是从 Cisco 官方下载的预编译 OpenH264 二进制；本声明不主张 Cisco 对官方预编译二进制提供的专利许可适用于该构建。正式分发前，发布者仍需独立确认适用地区的 H.264 专利许可要求。

## MotionWallpaper

动态桌面壁纸组件的实现方式参考并部分移植自 MotionWallpaper，该项目采用 MIT License。

- 项目主页：https://github.com/1114656/MotionWallpaper
- 仓库内许可证副本：`third_party/MotionWallpaper-MIT.txt`

## 图标与素材

- `Desktop Sticker/assets/weather/S2/` 天气图标来自 [qwd/WeatherIcon](https://github.com/qwd/WeatherIcon)（和风天气），遵循 CC BY 4.0，许可文本见同目录 `LICENSE-CC-BY-4.0.txt`。
- `third_party/nlohmann/json.hpp`：JSON 解析，MIT License。
