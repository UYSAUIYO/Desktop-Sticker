# Wallpaper Studio（壁纸编辑器）工具与密钥

这个目录放**独立壁纸编辑器**（`dstk-studio.exe`）的构建时资产与工具脚本。设计见
`docs/superpowers/specs/2026-10-13-2d-wallpaper-studio-design.md`，实施计划见
`docs/superpowers/plans/2026-10-13-2d-wallpaper-studio.md`。

## 目录内容

| 文件 | 作用 |
|---|---|
| `signing_key.txt` | 64 位十六进制的 AES-256 密钥。**全仓库唯一定义处。** |
| `verify_key.ps1` | 校验密钥文件合法且非全 0 |
| `pack_encrypted.gd` | 把一个目录打包成加密 PCK（headless 编辑器执行） |
| `pack_encrypted.ps1` | 上面那个的调用壳 |
| `run_tests.ps1` | 无头测试（先同步工程到 `<exeDir>\studio\`，再跑 GDScript 用例） |
| `project/` | Studio 的 Godot 工程：`src/doc`（文档模型）、`src/build`（文档→场景）、`src/export`（海报/加密包/.dswall）、`src/ui`（三栏界面）、`src/cli`（无头导出）、`tests/` |

## 用编辑器做一个壁纸

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File build.bat
"D:/project/Desktop Sticker/Desktop Sticker/x64/Release/Desktop Sticker/dstk-studio.exe"
```

1. 左上「＋ 添加」加图层：**纯色 / 图片 / 文字 / 粒子**。列表顶部是最上层，底部按钮可删除、上移、下移、隐藏。
2. 中间画布就是导出后桌面上的样子（预览和导出走同一条 `SceneBuilder` 路径）：
   文字图层的「内容」选**时钟 / 日期**会真的走时；粒子不选贴图时用内置圆点。
3. 右侧属性面板按图层类型给控件；图片会**拷进工程 `assets/`**，文档里只存相对路径。
4. 「保存」写 `wallpaper.json`；工程默认在
   `%APPDATA%\Godot\app_userdata\桌面贴纸 · 壁纸编辑器\projects\<名称>\`（新建后第一次保存才创建目录）。
5. 「导出」写 `.dswall`；「**导出并应用到桌面**」导出到 `user://exports/` 并投递到
   桌面贴纸收件箱（`%APPDATA%\DesktopSticker\wallpaper_inbox\`），桌面贴纸会在几秒内自动导入并应用。

快捷键：`Ctrl+N` 新建、`Ctrl+O` 打开、`Ctrl+S` 保存、`Ctrl+E` 导出、`空格` 播放/暂停。

无头导出（CI/批处理，不弹窗；无头模式没有海报，`.dswall` 里就没有 `poster.png`）：

```bash
dstk-studio.exe --headless --script res://src/cli/export_cli.gd -- \
    --project <工程目录> --out <输出.dswall> [--inbox]
```

## 时间轴（阶段 2）

底部时间轴对**选中的内容图层**给出五条通道：`X 位置 / Y 位置 / 缩放 / 旋转 / 不透明度`。

| 操作 | 结果 |
|---|---|
| 左侧通道行的「＋」 | 在播放头给该通道加一个关键帧（值 = 当前采样值，画面不跳） |
| 双击轨道空白 | 同上：在该通道的该时刻插入关键帧 |
| 拖动关键帧 | 改时间（0.05s 吸附，按住 Shift 不吸附）；不会越过相邻关键帧 |
| 右键关键帧 / `Del` | 删除 |
| 点标尺或拖播放头 | 画布实时预览该时刻的姿态 |
| 顶栏「时长」 | `loop_seconds`，播放到末端回到 0。**它是权威值**：改短就是真的变短 |

- 导出时这些关键帧编译成一个 `AnimationPlayer`（预览里也有，只是不自动播）——
  导出后由引擎自己播放，不需要额外运行时脚本。
- **预览和导出共用一个求值器**：拖播放头 / 播放，都是让预览树里那个 `AnimationPlayer`
  真的 `seek`，所以看到的曲线就是导出后的曲线。
- `ease` 是 `linear` 之外的值时，编译阶段会按 60Hz 把这一段烘焙成多个线性关键帧
  （`Animation` 的线性轨道没有逐段曲线）。导出包里看到的是烘焙结果，文档里仍只存原始
  关键帧 + `ease`。
- `X/Y` 关键帧存的是**父级比例**（0..1，可越界），和静态图层的锚点是同一个量。
  所以换画布尺寸、缩放容器、移动父级都**不用改关键帧**，运动幅度自动跟着伸缩；
  只有「拖进/拖出容器」会重算（新旧父级坐标系不同，`reparent` / `wrap_in_group` 里处理）。
- 文档格式随之升到 `format_version: 2`：`1` 的 x/y 关键帧是像素，加载时一次性折算成比例。
- 缩放以框的左上角为原点；`rotation`、`opacity` 直接写节点属性。
- 拖动一个**已经有关键帧**的图层时，画布会把它写进播放头那一帧（自动关键帧）。
  它只对已有轨道的通道生效，不会凭空给静态图层加动画。
- 属性面板是**时间感知**的：有关键帧的通道显示播放头处的值（X/Y 用像素），
  改数字就是写当前帧的关键帧；没有关键帧的通道仍改基础值。
- 「缩放」只能通过关键帧表达（静态路径不读旧的 `transform.scale`），
  所以改这个数字会自动建出 `scale` 轨道。

## 动图（GIF / 帧序列）

图片图层的属性面板里有「导入动图…」：

- **导入 GIF**：用仓库自带的 `tools/ffmpeg/ffmpeg.exe` 把 GIF 拆成 PNG 帧
  （找不到 ffmpeg 时明确报错，不会静默失败）。帧率从 `ffmpeg -i` 的输出里探测。
- **导入帧序列目录**：把目录里的 `png/jpg/webp/bmp/tga` 按文件名排序拷进
  `assets/<名字>_frames/`，覆盖同名目录时自动加 `_2`、`_3` 后缀。
- 帧序列存在图层的 `frames` 里，**优先于单张 `src`**；`src` 会同时指向首帧，用作海报/兜底。
- 预览用 `AnimatedTexture`；导出时逐帧写进加密包，运行时由 `DSTK_IMAGE_SCRIPT` 解码，
  所以包体积基本等于源素材体积（不会把像素内联进场景）。

## 已知边界

- 图层类型只有 `color` / `image` / `text` / `particles` / `group`，以及独立的遮罩层/滤镜层；
  **没有逐帧绘制的时间轴**（只动属性，不做像素动画）。
- 效果图层（遮罩/滤镜）的几何是着色器 uniform，**不支持关键帧**；时间轴会给提示而不是假装能加。
- `ease` 目前只能手改 `wallpaper.json`（时间轴面板还没有缓动下拉框）；值仍然会正确烘焙进导出。
- 画布只做等比缩放预览，没有缩放/平移工具与图层框选；改叠放顺序用「上移 / 下移」。
- 没有撤销/重做；时间轴的关键帧也一样（删错了只能重新加）。

## `signing_key.txt` 为什么入库

它不是服务端凭据，而是**分发密钥**：

- 运行时（`godot.windows.template_release.x86_64.dll`）要靠它解密 `.dswall` 里的 `project.pck`；
- 编辑器（Studio）要靠它**写**加密包。

两者用的是同一个值，所以它必须随仓库一起走。**不要重新生成它** —— 一旦更换，所有旧包
和所有已分发的运行库都会对不上。

## 密钥是怎么进引擎的

引擎里 `script_encryption_key` 是一个**编译期常量**（`core/object/script_language.h` 里 `extern`，
实现在 `core/script_encryption_key.gen.cpp`）。非编辑器构建读加密 PCK 时**只能**用它 ——
宿主没有任何传密钥的入口（见 `core/io/file_access_pack.cpp` 里的 `script_encryption_key` 分支）。

所以 `tools/build_godot.ps1` 每次构建前都会拿 `signing_key.txt` 重写那个 `.gen.cpp`，
再开始编译。全 0 表示"未启用加密"，所以脚本拒绝全 0 —— 以全 0 构建出来的运行库读不了加密包。

## 这套加密挡什么、不挡什么

**挡：** 随手解压 `.dswall` 看内容、改内容、重新打包。加密包连目录索引都是密文，
解压工具看不到任何文件名。

**不挡：** 有动机的人。密钥烧在 DLL 里，拆 DLL 就能拿到。它**不是安全边界**。

**也不解决**「能不能安全运行陌生人的壁纸包」这个问题。如果包里带 GDScript，那些脚本会在
桌面贴纸进程里拿到完整的 Godot API（读写文件、联网、`OS.execute`）。加密之后这个事实
一点没变，只是别人看不见而已。真正的边界是 `.zcode/plans/` 里 M3b 的沙箱。

## 验证加密链路真的生效

正向（必须成功）：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/pack_encrypted.ps1 \
  -Source tools/godot-wallpaper-template -Out build_tmp/template_enc.pck
tools/godot-host-test/dstk-godot-host.exe --pack build_tmp/template_enc.pck --top --auto-exit 8
```

期望：正常渲染，日志里没有解密相关错误。

反向（必须失败）—— 用**另一把密钥**打同一个目录，再用运行库加载：

```
ERROR: The MD5 sum of the decrypted file does not match the expected value.
ERROR: Can't open encrypted pack directory.
ERROR: Cannot open resource pack '...'.
```

反向这一条是关键：没有它就无法区分"真的解密了"和"根本没加密"。它比"用全 0 密钥重建引擎"
便宜得多，证据等价，所以作为常规回归手段。

## 一个已经踩过的坑

`tools/build_godot.ps1` 依赖 AccessKit 与 D3D12 的预编译依赖（在 `bin/build_deps/`，gitignored）。
它们丢过一次，表现为构建直接报错：

```
ERROR: The screen reader support driver requires dependencies to be installed.
ERROR: The Direct3D 12 rendering driver requires dependencies to be installed.
```

修法（需要网络）：

```bash
cd tools/godot-src
py -3 misc/scripts/install_accesskit.py
py -3 misc/scripts/install_d3d12_sdk_windows.py
```

不要用 `accesskit=no d3d12=no` 绕过：AccessKit 的 Windows 子类化适配器在壁纸场景里是必需的
（见 `AGENTS.md` 的脆弱区）。
