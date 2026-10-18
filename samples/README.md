# samples — 可直接加载的场景壁纸包

仓库自带两个 `.dswall` 场景包，用来复刻验收、做输入回归，以及给新改动的渲染/输入链路当对照物。
它们只服务于开发，**不随安装包分发**。`.gitignore` 默认忽略所有 `*.dswall`（编辑器插件和测试的导出物），
这里用 `!samples/*.dswall` 开了例外；其它目录下导出的包仍然不会进版本。

| 文件 | 标题 | 大小 | 用途 |
|---|---|---|---|
| `DesktopSticker Wallpaper Scene.dswall` | DesktopSticker Wallpaper Scene | 191 KB | 通用示例场景：验证导入、海报缩略图、渲染、切包 |
| `3d-keyboard-test.dswall` | 3D 键盘 / 鼠标测试台 | 5.6 KB | 输入回归台：键帽实时高亮 + HUD 计数，覆盖单键、组合键、左/右/中键、滚轮、左键拖动 |

> 文件名用 ASCII、包内标题保持中文：`dstk-godot-host.exe` 调 `tar.exe` 解包时走 ANSI 参数，
> 中文路径会变成 `?????` 解包失败，所以仓库里的包名不用中文。

两个包都是普通 ZIP 容器（`project.pck` + `manifest.json` + `poster.png`），PCK 未加密，可直接解压看结构；
加密只发生在 Wallpaper Studio 的「导出」（见 `tools/wallpaper-studio/`）。

## 怎么加载

1. 设置页 →「桌面壁纸」→ 导入，选 `.dswall`（或把包拖进壁纸列表）；
2. 不启 GUI 的排查/回归：`tools/godot-host-test/dstk-godot-host.exe --dswall samples\3d-keyboard-test.dswall --auto-exit 4`
   （它会把包加载进一个 `WorkerW` 子窗口、打 fps，见 `tools/godot-host-test/`）。

## 3D 键盘 / 鼠标测试台：期望表现

验收步骤见 `docs/acceptance.md` 的「交互壁纸 / 键盘」。进入壁纸后 HUD 的六行分别是：

| HUD 行 | 含义 |
|---|---|
| `按住键：A, S（共 2 个）` | 当前所有按下的键；**组合键**验证看这里要同时出现多个键 |
| `按下 N 次 · 松开 M 次 · 最近：…` | 累计计数，以及最近一次按键的 keycode / physical / unicode |
| `鼠标位置：x, y（视口 W x H）· 当前按下：左` | 光标在场景里的坐标与当前按住的鼠标键 |
| `左键 … 右键 … 中键 … 滚轮 上/下` | 鼠标按键与滚轮的累计计数 |
| `拖动：进行中 · 本次 N px · 上次 M px` | 左键拖动累计像素距离，松开后"本次"写进"上次" |
| `光点屏幕偏差：N px` | 自检：光标射线打到桌面平面后投回屏幕，与指针的偏差；0 = 光点压在指针正下方 |

键帽会随着真实按键实时按下/弹起（`Input.is_physical_key_pressed` + 事件缓存双保险），所以组合键、
长按、修饰键都能一眼看出来。

引擎侧还会把收到的每条输入事件写进 `%APPDATA%\Godot\app_userdata\3D Keyboard Input Test\input.log`
（`user://input.log`，每次进入场景重建）。排查"重复投递 / 坐标偏移"时直接看这个文件，
`docs/acceptance.md` 里的键盘条目就靠它取证据。

## 重建测试包

源工程在 `samples/keyboard-test-src/`：纯程序化建模（无贴图、无字体依赖），template_release 运行库可直接跑。

```powershell
# 运行库先编一次：tools/build_godot.ps1，产物在 tools/godot-src/bin/
tools/godot-src/bin/godot.windows.template_release.x86_64.exe --headless --path samples/keyboard-test-src --script res://tools/pack.gd
# 产物在 samples/keyboard-test-src/out/（已 gitignore），拷回 samples/ 覆盖旧包
```

打包脚本同时生成海报和清单（标题「3D 键盘 / 鼠标测试台」），产物名 `3d-keyboard-test.dswall`。
改了场景脚本后要重新打包并覆盖 `samples/3d-keyboard-test.dswall`，否则仓库里留的是旧版本。
