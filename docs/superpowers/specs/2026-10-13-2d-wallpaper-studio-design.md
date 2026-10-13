# 2D 壁纸编辑器（Wallpaper Studio）设计

状态：2026-10-13 设计定稿
关联：
- `docs/superpowers/specs/2026-09-25-wallpaper-backends-design.md`（壁纸后端与 `.dswall` 包格式）
- `.zcode/plans/plan-sess_7d1d32b9-….md`（Godot fork + 内嵌运行时的 M1–M4 计划）
- `DesktopSticker.WallPaper/include/desktopsticker/wallpaper/WallPaperPackage.h`（包解析）

---

## 1. 目标与边界

### 1.1 目标

做一个**独立交付**的 2D 壁纸编辑器 `dstk-studio.exe`：

- 它是一个 **Godot 程序**：宿主 EXE 加载我们 fork 出的 `libgodot`，编辑器界面本身是一个 Godot 工程。
- 用户面对的是**专用界面**（图层列表 / 画布预览 / 属性面板 / 时间轴），看不到 Godot 的节点树和脚本。
- 产物是**加密的 `.dswall`**，交给桌面贴纸导入和渲染，桌面贴纸侧不改导入逻辑。
- 全程不需要用户安装或启动任何 Godot 程序。

### 1.2 非目标（明确排除）

| 不做 | 原因 |
|---|---|
| 替换 / 内嵌 Godot 官方编辑器 | 用户选了专用编辑器；官方编辑器只在 M3b 的既有路径里保留 |
| 3D 场景编辑 | 3D 壁纸交给 Godot 编辑器模板或既有 Vulkan 内置场景 |
| 在线分发、商店、账号 | 与桌面贴纸的本地单机形态不符 |
| 自建渲染器 | 预览与导出全部复用引擎 |
| 从零写代码编辑器 | 「自定义脚本」只做**导入已写好的文件**，见 §8 |

### 1.3 决策记录（与既有工作的关系）

仓库里已经有一条**已投产**的编辑器路径，本设计不推翻它：

| 已有 | 处置 |
|---|---|
| `tools/godot-src/editor/editor_wallpaper_plugin.cpp`（M2：校验 + 导出 `.dswall`） | **保留**。它是「高级模式」入口，也继续支撑模板工程的手工创作。 |
| `.zcode/plans/…` 的 **M3b 权限沙箱** | **保留为独立工作**。Studio 的权限声明是「告知」，不是沙箱；沙箱落地后 Studio 无需改动。 |
| `tools/godot-src/editor/plugins/wallpaper_editor.{h,cpp}`、`wallpaper_effect_gallery.{h,cpp}`（未提交的 P2/P3 效果库） | **移植**。`wallpaper_effects_builtin.h` 里的 6 个效果着色器原样搬进 Studio 的效果库（§7.3）；编辑器内的停靠面板保留不删，但它不再是主线。 |
| `tools/godot-wallpaper-template/paint/`（未提交的画笔工具 + 着色器） | **并入**。`wallpaper_effects.gdshader` 作为「遮罩效果」着色器进入 Studio 效果库。 |
| `tools/godot-host-test/` | **复用为验收工具**：导出的 `.dswall` 用它加载验证。 |

---

## 2. 架构

### 2.1 组件图

```
┌──────────────────────────────────────────────────────────────────────┐
│ dstk-studio.exe   （新：DesktopSticker.WallpaperStudio，C++/Win32）    │
│   · LoadLibraryExW  <exeDir>\godot\godot.windows.template_release...dll│
│   · dstk_godot_create(argv = --path <exeDir>\studio)                   │
│   · 循环 dstk_godot_iteration()；iteration 返回 false 即退出           │
│   · 不调用 dstk_embed_set_present_hwnd → 引擎自建顶层窗口              │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ 同一个进程
┌───────────────────────────────▼──────────────────────────────────────┐
│ studio/   （新：Godot 工程，<exeDir>\studio\）                        │
│                                                                       │
│  UI 层        src/ui/      studio_main · layer_panel · canvas_view     │
│                            inspector · timeline_panel · dialogs        │
│  逻辑层      src/doc/      doc.gd · layer.gd · timeline.gd · migrate  │
│              src/build/    scene_builder.gd · layer_nodes.gd          │
│              src/export/   exporter.gd · packer.gd · poster.gd        │
│              src/effects/  builtin_effects.gd（移植 P2 的 6 个效果）   │
│  测试        tests/        run_tests.gd + test_*.gd（无头运行）        │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ 导出
┌───────────────────────────────▼──────────────────────────────────────┐
│ 加密的 .dswall（ZIP）                                                  │
│   project.pck  ← PCKPacker，AES-256，密钥烧进 libgodot                 │
│   manifest.json                                                       │
│   poster.png                                                          │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ 导入（不改现有逻辑）
┌───────────────────────────────▼──────────────────────────────────────┐
│ 桌面贴纸：MediaLibrary → GodotEngineHost → 内嵌 libgodot 渲染          │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 为什么 Studio 必须按**目录**分发

导出流程要在 `res://` 下生成场景和拷贝资源，再用 `ResourceSaver` 保存。神游要点：

- 若 `res://` 是一个 PCK（打包分发），它是只读的，`ResourceSaver.save()` 会失败；
- 若 `res://` 是一个真实目录，则 `<studio>/build/<pack_id>/` 可写，`ResourceSaver` 会把写进去的绝对路径
  **自动本地化**为 `res://build/<pack_id>/…`（`ProjectSettings::localize_path` 的行为，已核对源码
  `core/config/project_settings.cpp:157`），打包时路径天然对得上，不需要任何字符串重写。

所以：**Studio 以目录形式随应用分发**（`<exeDir>\studio\`），和 `assets/weather/`、`tools/godot/` 的做法一致。
桌面贴纸本身也是非打包（unpackaged）交付，这一点不引入新的约束。

### 2.3 组件职责与接口

| 单元 | 职责 | 依赖 | 不做什么 |
|---|---|---|---|
| `dstk-studio.exe` | 加载 DLL、解析命令行、驱动迭代、单实例互斥、日志 | libgodot 的 `dstk_*` 导出 | 不含任何编辑逻辑、不碰窗口样式 |
| `src/doc/doc.gd` | 读/写/校验 `wallpaper.json`，版本迁移 | JSON、`layer.gd`、`timeline.gd` | 不碰节点、不碰文件系统以外的 IO |
| `src/doc/timeline.gd` | 关键帧数据 + 在任意时刻 t 求值 | 无 | 不知道 UI、不知道节点 |
| `src/build/scene_builder.gd` | 文档 → `Node` 树（纯构造） | `layer_nodes.gd`、`builtin_effects.gd` | 不落盘、不导出 |
| `src/export/exporter.gd` | 编排：建目录 → 拷资源 → 建场景 → 截海报 → 打 PCK → 写 manifest → 打包 → 投递 | 上述全部 + `PCKPacker`/`ZIPPacker` | 不做 UI 反馈（返回结果对象） |
| `src/ui/*` | 界面与交互 | `doc`/`build`/`export` | 不直接操作节点树 |

**关键约定**：`scene_builder.gd` 同时被**预览**和**导出**使用。预览和导出走同一条构造路径，不存在
"预览对、导出错" 的可能。这是整个设计里最重要的不变式。

---

## 3. 文档模型（`wallpaper.json`）

### 3.1 顶层

```json
{
  "format_version": 1,
  "name": "我的壁纸",
  "canvas": { "width": 1920, "height": 1080, "background": "#06121f" },
  "loop_seconds": 8.0,
  "layers": []
}
```

- `format_version`：整数，从 1 起。加载器按版本走迁移链，**永远不因为多出字段而失败**。
- `canvas`：只作为设计参考与预览尺寸。运行时按壁纸窗口尺寸自适应（节点用锚点），
  所以画布比例与显示器不一致时不会拉伸变形 —— 只在属性面板里提示。

### 3.2 图层通用字段

```json
{
  "id": "l_3f2a",
  "type": "image",
  "name": "花瓣",
  "visible": true,
  "transform": { "x": 960, "y": 540, "scale": 1.0, "rotation": 0.0, "opacity": 1.0 },
  "blend": "mix",
  "tracks": [],
  "effects": []
}
```

`blend` 取值：`mix` / `add` / `sub` / `mul` / `screen` / `overlay`（映射到 `CanvasItem` 的
`canvas_item.blend_mode` 与 `modulate`；`mix` 是默认）。

`effects` 是**挂在这一层上的效果栈**（见 §3.5）。效果不是独立的图层类型 —— 这既是 Wallpaper Engine
的模型，也让 "效果作用于哪张图" 这件事没有歧义。

### 3.3 图层类型

| `type` | 专有字段 | 生成节点 |
|---|---|---|
| `color` | `color` | `ColorRect` |
| `image` | `src`（相对工程目录）、`fit`（`fill`/`contain`/`cover`/`tile`）、`speed` | `TextureRect`（`tile` 用 `TextureRect.stretch_mode=TILE`） |
| `text` | `text`、`font_size`、`color`、`align`、`dynamic`（`none`/`clock`/`date`）、`date_format` | `Label`（`dynamic != none` 时挂时钟脚本，见 §5.2） |
| `particles` | `texture`、`amount`、`lifetime`、`direction`、`spread`、`velocity`、`gravity`、`scale_min/max` | `GPUParticles2D` |

阶段划分（见 §9）决定哪些 `type` 先落地，但 schema 一次写全 —— 未实现的类型在加载时保留原样、
在 UI 里置灰，不丢数据。

### 3.5 效果栈

```json
"effects": [
  { "id": "e_91c4", "effect_id": "water", "enabled": true,
    "params": { "strength": 0.04, "scale": 8.0 },
    "mask": "assets/wave_mask.png" }
]
```

- 栈是**有序**的，按数组顺序逐趟烘焙（index 0 先跑）。
- `mask` 可选；存在时着色器拿到的 `mask` uniform 指向该贴图，缺失时效果退化为全区域。
- UI 上效果栈显示在所属图层下面（缩进一级），不是顶层图层条目。

### 3.4 时间轴

```json
"tracks": [
  { "channel": "y", "keys": [ { "t": 0.0, "v": 0.0, "ease": "linear" },
                              { "t": 8.0, "v": -240.0, "ease": "in_out" } ] }
]
```

- `channel` 取值：`x` / `y` / `scale` / `rotation` / `opacity`。
- `keys` 按 `t` 升序且**不要求**首尾到齐；缺失时用图层的基础变换值补。
- `ease` 取值：`linear` / `in` / `out` / `in_out`；求值走纯函数（§4.2）。
- 超过最后一个关键帧后**保持末值**（不循环回绕）—— 回绕交给动画资源自身的 loop 设置。

---

## 4. 纯逻辑（可单元测试，不依赖 Godot 运行时之外的东西）

这几块按仓库既有约定写成无副作用的函数，方便用 `tests/` 覆盖：

### 4.1 `doc.gd`

- `parse(text) -> {ok, doc, error}`：任何字段缺失/类型错误都取默认值，**只有 `format_version`
  无法识别才失败**。
- `to_text(doc) -> String`：稳定字段顺序，便于 diff。
- `validate(doc, project_dir) -> PackedStringArray`：资源是否存在、`effect_id` 是否存在、
  `dynamic` 与 `text` 冲突等。

### 4.2 `timeline.gd`

- `sample(track, t) -> float`：二分找区间 → 按 `ease` 插值；空关键帧返回 `null`（交由调用方回落基础值）。
- `sample_layer(layer, t) -> {x,y,scale,rotation,opacity}`：把各通道合成到基础变换上。
- 边界：`t` 在首帧前 → 首值；末帧后 → 末值；单关键帧 → 恒值。

### 4.3 效果库

`builtin_effects.gd` 是一个 `Array[Dictionary]`（`id` / `name` / `category` / `params` / `code`），
内容直接搬 `wallpaper_effects_builtin.h` 的 6 个效果 + `paint/wallpaper_effects.gdshader` 的遮罩效果：

| id | 名称 | 分类 | 参数 |
|---|---|---|---|
| `water` | 水波 | 动画 | strength / scale / speed / specular |
| `rain` | 雨 | 动画 | density / speed / wind |
| `blur` | 模糊 | 模糊 | radius |
| `twist` | 扭曲 | 动画 | strength / speed |
| `rotate` | 旋转 | 动画 | speed |
| `ripple` | 涟漪 | 互动 | amplitude / frequency / speed |
| `mask` | 三通道遮罩 | 遮罩 | strength / 三通道开关（用 `mask` 贴图） |

着色器源码在这里是**唯一的真源**；`wallpaper_effects_builtin.h` 不再新增效果。

---

## 5. 场景生成（文档 → Godot 场景）

### 5.1 结构

```
Main (Control, 锚点铺满)
├─ Background (ColorRect)              ← canvas.background
├─ Layer_<id>… (每层一个 CanvasItem)
│   │   transform → position/scale/rotation/modulate
│   └─ (effect 图层是 SubViewport 链，见 §5.3)
└─ Animation (AnimationPlayer)          ← 根上的 AnimationPlayer，名固定
```

- 图层顺序 = 子节点顺序（列表顶部 = 最上层 = 最后 add_child 或最高 index；UI 里显示的顺序与实际
  index 保持一致，由 `scene_builder` 保证）。
- 时间轴编译成 `AnimationPlayer` 里一个名为 `loop` 的 `Animation`，`length = loop_seconds`，
  `loop_mode = LOOP_LINEAR`。每条 track → 一条 `PropertyTrack`（`NodePath` 指向图层节点）。
- 缓动映射：`linear`→`TRANS_LINEAR`、`in`→`TRANS_CUBIC/EASE_IN`、`out`→`TRANS_CUBIC/EASE_OUT`、
  `in_out`→`TRANS_CUBIC/EASE_IN_OUT`。

**零关键帧特例**：`loop_seconds <= 0` 或所有 track 为空时不生成 `AnimationPlayer`，
场景就是静态的（省一次每帧求值）。

### 5.2 时钟/日期

`dynamic != "none"` 的文本图层需要运行时刷新。做法是让 `layer_nodes.gd` 持有一段**固定源码常量**，
预览和导出都从这同一份常量在内存里编译出 `GDScript` 挂到 `Label` 上：

```gdscript
# 常量 DSTK_CLOCK_SCRIPT 的内容（不读任何外部文件）
extends Label

@export var mode: String = "clock"
@export var date_format: String = "%04d-%02d-%02d"

func _ready() -> void:
	_refresh()

func _process(_delta: float) -> void:
	_refresh()

func _refresh() -> void:
	var now := Time.get_datetime_dict_from_system()
	if mode == "date":
		text = date_format % [now.year, now.month, now.day]
	else:
		text = "%02d:%02d:%02d" % [now.hour, now.minute, now.second]
```

责任划分：`layer_nodes.gd` 提供常量并在内存里编译、赋值 `mode`/`date_format`；
`scene_builder.gd` 构造节点树；`exporter.gd` 只管 `ResourceSaver` —— 因为它拿到的是一个
**没有磁盘路径的 `GDScript`**，`PackedScene.pack` 会把它作为 `sub_resource` 内联进 `.tscn`。

选内联而不是写成 `assets/dstk_clock.gd` 的原因：内联后不存在"预览用的那份"和"导出用的那份"两个文件，
"同一份源码"这条不变式更强；JSON 里也不用多一个字段去记录生成文件。

这是**唯一**由 Studio 生成的脚本，它不含任何 IO/网络，是纯展示逻辑，**不计入** `content_flags`。

### 5.3 效果栈（挂在一个图层上的 SubViewport 链）

沿用未提交的 P2 实现思路（`wallpaper_editor.cpp::_rebuild_preview`），但**烘焙进场景**而不是
预览时临时搭。带 `n` 个效果的图层 `L` 生成：

```
L_pass0 (SubViewportContainer, 与 L 同尺寸同位置)
└─ SubViewport (透明底, UPDATE_ALWAYS)
    ├─ Source (Sprite2D, 纹理 = 该层原本的内容)
    └─ material = 效果 0 的 ShaderMaterial
L_pass1 (SubViewportContainer, 同上)
└─ SubViewport
    └─ Source (Sprite2D, 纹理 = L_pass0 的 SubViewportTexture)
    └─ material = 效果 1
…
L_passN ── 就是图层 L 在场景里的实际节点（替代原来的 `TextureRect`/`Label`/…）
```

也就是说：**图层本身被效果链替换**，链的第 0 趟吃的是图层原始内容，最后一趟就是可见结果。
因此效果只影响它所属的图层，不影响别层 —— 与 §3.5 的语义一致。

代价：每个效果多一个等尺寸 render target。属性面板在栈深 > 3 时给出掉帧提示。

### 5.4 粒子图层

`GPUParticles2D` + `ParticleProcessMaterial`，参数直接映射到材质属性：

| 文档字段 | 材质属性 |
|---|---|
| `amount` / `lifetime` | 节点 `amount` / `lifetime` |
| `direction` / `spread` | `direction` / `spread` |
| `velocity` | `initial_velocity_min/max` |
| `gravity` | `gravity` |
| `scale_min/max` | `scale_min/max` |

导出时粒子材质作为 `sub_resource` 内联进 `.tscn`，不产生额外文件。

---

## 6. 导出与加密

### 6.1 密钥

- 32 字节密钥，以 64 位十六进制字符串存在**仓库内单一定义处**：
  `tools/wallpaper-studio/signing_key.txt`。
- 构建 fork 时由 `tools/build_godot.ps1` 读该文件，生成 `core/script_encryption_key.gen.cpp`
  （替换当前全 0 的占位内容）。
- Studio 侧读同一个文件（构建时由 PostBuildEvent 拷进 `studio/`，并 gitignore 掉拷贝产物）。
- **诚实声明**：密钥烧进 `godot.windows.template_release.x86_64.dll`，能被有动机的人提取。
  加密的目标是「挡住随手解压和改包」，**不是安全边界**。真正的边界是 M3b 的沙箱。

### 6.2 打包

用**运行时** API，不需要编辑器：

```gdscript
var packer := PCKPacker.new()
packer.pck_start(pck_path, 32, KEY_HEX, true)          # encrypt_directory = true
packer.add_file(target_res_path, disk_path, true)      # 逐文件加密
packer.flush()
```

- `target_res_path` 用 `res://build/<pack_id>/…` 相对路径，与 `main.tscn` 里的引用一致。
- 引擎侧读包时走编译进 DLL 的密钥（`core/io/file_access_pack.cpp` 的 `script_encryption_key`
  分支），无需宿主传密钥。

### 6.3 构建目录

```
<studio>/build/<pack_id>/          ← 每次导出前整目录删除重建
├─ main.tscn                       ← ResourceSaver.save(PackedScene)
│                                    时钟脚本作为 sub_resource 内联在内
└─ assets/<用户资源拷贝>           ← 贴图按文件名平铺
```

海报与 PCK 是中间产物，写在 `user://scratch/<pack_id>/` —— **不能**放进上面这个目录，
否则 `packer` 把构建目录整目录扫描时会把中间产物也打进包里。
（`StudioPaths.scratch_dir()` 是这条路径的唯一计算处。）

`pack_id`：工程首次保存时生成的 16 字节随机十六进制串，存在 `wallpaper.json` 同级。

### 6.4 manifest

在现有字段（`format_version`/`title`/`author`/`description`/`pack_id`/`dimension`/`tags`/
`categories`/`main_scene`/`engine`/`created_utc`/`requested_permissions`）之外新增：

```json
"content_flags": ["scripts"]        // 见 §8
```

`dimension` 固定写 `"2d"`。

### 6.5 `.dswall` 容器

保持 ZIP：`project.pck` + `manifest.json` + `poster.png`。**格式版本仍是 1**，
`WallPaperPackage.h` 无需改动（`content_flags` 是新可选字段，解析器忽略未知字段）。

### 6.6 投递与原子性

1. 导出到 `<目标目录>/<name>.dswall.tmp`；
2. 校验（见 §6.7）通过后 `rename` 成 `<name>.dswall`，**覆盖前不删除旧文件**；
3. 「应用到桌面」在此基础上把成品复制到 `%APPDATA%\DesktopSticker\wallpaper_inbox\<name>.dswall`
   （先写 `.tmp` 再改名），沿用 `plugin.gd` 已有约定。

### 6.7 导出前校验（阻塞导出）

- 工程目录可写、磁盘剩余空间 > 资源总大小 × 2；
- 每个图层引用的资源存在且可读；
- `effect_id` 在效果库里存在；粒子参数在合法范围；
- 画布尺寸合法（64…16384）；
- 存在至少一个可见图层（否则导出的是一张纯色图，允许但要警告）。

校验失败 → 面板列出问题 + 定位到出问题的图层，**不生成任何文件**。

---

## 7. 权限声明

### 7.1 数据来源

Studio 在导出时推导并写入 `requested_permissions`（沿用现有 4 个 id）：

| id | 触发条件 |
|---|---|
| `network` | 工程里存在引用外部 URL 的图层参数（当前无此类图层 → 不会出现） |
| `file_outside` | 存在导入的外部脚本，且脚本文本里出现 `FileAccess` / `DirAccess` |
| `exec` | 脚本里出现 `OS.execute` / `OS.create_process` |
| `system` | 脚本里出现 `OS.get_*` / `DisplayServer` / 输入注入类调用 |

推导规则是**保守的文本扫描**（纯函数，可测）。扫描不出来的一律不声明 —— 宁可少声明也不虚报。

### 7.2 `content_flags`

导出时若构建目录里存在 `.gd` 脚本（不含 Studio 生成的那个时钟脚本），写入
`content_flags: ["scripts"]`。

### 7.3 桌面贴纸侧展示

`WallPaperStore` / 设置页读取 `content_flags` 与 `requested_permissions`：

- 有 `requested_permissions` → 条目详情里逐项列出；
- 有 `content_flags` 含 `scripts` → 条目上打**醒目徽标**，详情页顶部一条黄色警示
  「此壁纸包含脚本，会在桌面贴纸进程内运行」；
- 当前**不拦截**（M3b 沙箱落地后才谈拦截），但措辞必须诚实，不能暗示已经限制。

---

## 8. 自定义脚本 / 着色器（用户选的第 6 项）

范围严格限定，避免把 Studio 变成代码编辑器：

- **导入**：用户可以选一个 `.gdshader` 或 `.gd` 文件，作为图层挂上去。Studio **不提供编辑**
  （双击用系统默认程序打开）。
- 着色器：作为 `ShaderMaterial` 的 `shader` 挂到图层，参数用 `uniform` 自动扫出来生成控件。
- 脚本：挂到图层节点上。
- 导入的脚本会参与 §7.1 的权限推导。

---

## 9. 阶段划分

每个阶段都在**上一个阶段已经可用的基础上**增加能力，不做半成品合并。

| 阶段 | 内容 | 完成标志 |
|---|---|---|
| **0** | 密钥烧入 + 加密导出 + 权限/`content_flags` 展示 | 现有模板工程导出加密包 → 桌面贴纸正常加载；解压工具看不到内容；设置页显示权限徽标 |
| **1** | Studio 宿主 EXE + Godot 工程 + 文档模型 + 三栏界面 + 导出闭环（图层类型只有 `color`/`image`/`text`，无时间轴） | 不开 Godot 编辑器，从零做一个静态壁纸并应用到桌面 |
| **2** | 时间轴数据模型 + 时间轴面板 + `AnimationPlayer` 编译 + `image` 动图 | 做一个「GIF 花瓣飘落」壁纸并应用 |
| **3** | 图层效果栈（6 个内置效果 + 1 个遮罩效果 + 遮罩画笔）+ 粒子图层 + 导入着色器/脚本 | 做一个「雨 + 水波 + 粒子」壁纸并应用 |
| **4** | 鼠标视差 | 鼠标在桌面移动时图层按深度偏移 |

**阶段 4 的独立风险**：桌面壁纸窗口是 `HTTRANSPARENT` 鼠标穿透的，Godot 收不到鼠标移动。
需要宿主把光标位置喂进引擎（新增一个 `dstk_*` 导出 + 一个自动加载单例）。这条改的是桌面贴纸
的壁纸宿主，与 Studio 解耦，放到最后。

---

## 10. 错误处理

| 场景 | 行为 |
|---|---|
| `wallpaper.json` 损坏 | 备份为 `wallpaper.json.bak`，以空工程启动，明确提示 |
| `format_version` 高于当前 | 拒绝加载并提示「请升级桌面贴纸」，**不尝试猜测** |
| 资源文件丢失 | 图层在列表和画布上标红，允许保存；导出被校验阻塞 |
| 导出中途失败 | 只留 `.tmp`，不产生半个 `.dswall`；`build/` 目录保留供排查 |
| PCK 写入失败 | 直传引擎错误码 + 磁盘剩余空间 |
| 投递收件箱失败 | 提示已导出的完整路径，用户可手动导入 |
| 引擎 DLL 缺失/版本不符 | 宿主 EXE 弹 MessageBox 并 exit 3，不静默退出 |
| Studio 崩溃 | 崩溃随进程；工程文件是纯 JSON，已保存的部分不丢 |

---

## 11. 测试策略

### 11.1 Studio 逻辑（无头 Godot 测试）

新增一个极小的测试运行器，**不引入 GUT/GdUnit**：

```bash
dstk-studio.exe --path <studio> --headless --script res://tests/run_tests.gd --quit-after 600
```

`run_tests.gd` 用 `dtest` 同款的极简断言（`describe`/`assert_eq`/`assert_true`），
失败时 `quit(1)` 并打印，全通过 `quit(0)`。覆盖：

- `test_doc.gd`：JSON 往返、缺字段取默认、未知 `format_version` 拒绝、迁移链
- `test_timeline.gd`：区间插值、四种缓动、首尾保持、单关键帧、空 track
- `test_scene_builder.gd`：图层数量/顺序/节点类型、带效果栈的图层被替换成 `SubViewport` 链且
  链长 = 效果数、时间轴 → 轨道数量、无关键帧不生成 `AnimationPlayer`、时钟脚本注入
- `test_effects.gd`：7 个效果定义完整（id 唯一、着色器能编译、参数有默认值）
- `test_permissions.gd`：文本扫描推导的保守性（没写的关键字绝不声明）
- `test_exporter.gd`：产物 ZIP 里三个条目齐全、manifest 字段正确、加密包能被
  `ProjectSettings.load_resource_pack` 挂载并读到 `main.tscn`

> 最后一条会在 Studio 进程里挂载导出的包。因为引擎是进程级单例、挂载不可卸载，
> 所以这个用例要在**独立的 runner 进程**里跑（`run_tests.gd` 支持 `--only=<name>`）。

### 11.2 端到端（人工，工具脚本）

沿用现有 `tools/`：

1. `dstk-godot-host.exe --dswall <导出物>` 加载 → 目视确认渲染；
2. `tools/screen_capture.ps1` 截图存档做前后对比；
3. 完整的「导入 → 应用 → 切换回视频壁纸 → 退出恢复桌面图标」按 `docs/acceptance.md` 走一遍。

### 11.3 接入构建

`tools/dev_build.ps1` / `rebuild_and_test.ps1` 增加一步：Studio 无头测试（可 `-NoStudio` 跳过）。
宿主 EXE 提供 `--self-test`：加载 DLL、创建实例、跑 3 帧、`quit(0)`，用于快速冒烟。

---

## 12. 风险

| 风险 | 影响 | 对策 |
|---|---|---|
| 加密密钥可从 DLL 提取 | 加密形同虚设 | 已在 §6.1 明确为告知性；不把它当安全边界 |
| `ResourceSaver` 在 template_release 下的路径本地化行为与预期不符 | 导出的场景引用错路径 | 阶段 1 第一件事就是跑通「生成 .tscn → 打包 → 宿主加载」的最小闭环，早暴露 |
| Studio 依赖可写的 `res://` | 以 PCK 分发时导出失效 | 固定按目录分发；宿主启动时检测 `res://` 是否可写，不可写直接报错而不是静默失败 |
| 效果图层每层一个 SubViewport | 4 层以上掉帧 | 属性面板阈值提示；后续可合并成一趟多效果着色器 |
| GDScript 无头测试是新增基础设施 | 维护成本 | 运行器控制在 100 行内，不引第三方框架 |
| fork 每次构建数小时 | 迭代慢 | 阶段 0 之后基本不再改引擎；Studio 迭代只跑宿主 EXE |
| 与既有 Godot 编辑器插件的功能重叠 | 两套东西 | §1.3 已明确：插件降级为高级模式，效果库单一真源在 Studio |
