# 2D 壁纸编辑器（Wallpaper Studio）阶段 0 + 阶段 1 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付一个独立运行的 `dstk-studio.exe`：加载 fork 的 Godot 运行库，用 Godot 工程做的三栏界面编辑图层文档，导出**加密的** `.dswall` 并一键投递到桌面贴纸。

**Architecture:** Studio 宿主 EXE 是极薄的 libgodot 驱动层（约 200 行 C++），不含任何编辑逻辑；编辑器本体是 `<exeDir>\studio\` 下的一个 Godot 工程（GDScript）。文档是纯 JSON（`wallpaper.json`），预览与导出共用同一个 `scene_builder.gd` 把文档编译成 Godot 场景，再用 `ResourceSaver` 写进 `<studio>/build/<id>/`、用 `PCKPacker` 打成 AES-256 加密的 `project.pck`、用 `ZIPPacker` 装成 `.dswall`。解密密钥编译进 fork 的 `script_encryption_key.gen.cpp`。

**Tech Stack:** C++20 / Win32（宿主 EXE，MSBuild 解决方案内）、Godot 4.7.2 fork（GDScript 编辑器工程）、`PCKPacker` / `ZIPPacker` / `ResourceSaver` 运行时 API、PowerShell 构建脚本。

**范围说明:** 本计划覆盖 spec 的**阶段 0 与阶段 1**（`docs/superpowers/specs/2026-10-13-2d-wallpaper-studio-design.md`）。阶段 2（时间轴）/ 3（效果栈·粒子·自定义着色器）/ 4（鼠标视差）各自单独成计划，从同一份 spec 派生；本计划末尾 §"阶段 2–4 的接缝" 记录它们必须遵守的约束。

---

## 文件结构

先定结构，再拆任务。每个文件一个职责。

### 新增

| 路径 | 职责 |
|---|---|
| `tools/wallpaper-studio/signing_key.txt` | 64 位十六进制 AES-256 密钥，**全仓库唯一定义处** |
| `tools/wallpaper-studio/pack_encrypted.gd` | 用编辑器 headless 执行：把任意目录打包成加密 PCK（阶段 0 的验证工具） |
| `tools/wallpaper-studio/pack_encrypted.ps1` | 上面那个的调用壳 |
| `tools/wallpaper-studio/README.md` | Studio 的构建、运行、测试说明 |
| `Desktop Sticker/DesktopSticker.WallpaperStudio/DesktopSticker.WallpaperStudio.vcxproj` | 宿主 EXE 工程（进 .sln） |
| `Desktop Sticker/DesktopSticker.WallpaperStudio/DesktopSticker.WallpaperStudio.vcxproj.filters` | 同上 |
| `Desktop Sticker/DesktopSticker.WallpaperStudio/main.cpp` | 宿主全部代码：加载 DLL、驱动迭代、单实例、`--self-test` |
| `tools/wallpaper-studio/project/project.godot` | Studio 的 Godot 工程配置 |
| `tools/wallpaper-studio/project/main.tscn` | 三栏界面根场景 |
| `tools/wallpaper-studio/project/src/ui/studio_main.gd` | 界面总装：持有 `StudioDoc`，串起各面板 |
| `tools/wallpaper-studio/project/src/ui/layer_panel.gd` | 图层列表（增删/显隐/改名/上下移） |
| `tools/wallpaper-studio/project/src/ui/canvas_view.gd` | 画布：`SubViewport` 里放 `scene_builder` 造出来的实时预览树 |
| `tools/wallpaper-studio/project/src/ui/inspector.gd` | 选中图层的属性控件 |
| `tools/wallpaper-studio/project/src/ui/project_bar.gd` | 新建/打开/保存/导出/应用到桌面 工具条 |
| `tools/wallpaper-studio/project/src/doc/doc.gd` | `StudioDoc`：JSON 读写、校验、图层增删改、信号 |
| `tools/wallpaper-studio/project/src/doc/layer.gd` | `Layer` 的构造与默认值、通用字段读写 |
| `tools/wallpaper-studio/project/src/doc/paths.gd` | 工程目录 / 构建目录 / 收件箱 路径计算（纯函数） |
| `tools/wallpaper-studio/project/src/build/layer_nodes.gd` | 每个图层类型 → 节点（含时钟脚本常量） |
| `tools/wallpaper-studio/project/src/build/scene_builder.gd` | 文档 → 完整 `Node` 树（预览与导出共用） |
| `tools/wallpaper-studio/project/src/export/packer.gd` | 加密 PCK + `.dswall` 容器 |
| `tools/wallpaper-studio/project/src/export/poster.gd` | 离屏渲染一帧出海报 |
| `tools/wallpaper-studio/project/src/export/manifest.gd` | manifest 组装 |
| `tools/wallpaper-studio/project/src/export/exporter.gd` | 导出编排 + 导出前校验 + 结果对象 |
| `tools/wallpaper-studio/project/src/effects/builtin_effects.gd` | 效果库数据（阶段 1 只定义与测试，阶段 3 才用） |
| `tools/wallpaper-studio/project/tests/test_framework.gd` | 极简断言与用例注册 |
| `tools/wallpaper-studio/project/tests/run_tests.gd` | 无头测试入口，`--only=<name>` 支持 |
| `tools/wallpaper-studio/project/tests/test_doc.gd` | 文档往返 / 默认值 / 版本拒绝 |
| `tools/wallpaper-studio/project/tests/test_scene_builder.gd` | 节点类型 / 顺序 / 时钟脚本注入 |
| `tools/wallpaper-studio/project/tests/test_effects.gd` | 效果库定义完整性 |
| `tools/wallpaper-studio/project/tests/test_paths.gd` | 路径纯函数 |
| `tools/wallpaper-studio/project/tests/test_exporter.gd` | 产物 ZIP / manifest / 加密包可挂载（独立进程） |

### 修改

| 路径 | 改什么 |
|---|---|
| `tools/build_godot.ps1` | 构建前读 `signing_key.txt` 生成 `core/script_encryption_key.gen.cpp` |
| `tools/godot-src/core/script_encryption_key.gen.cpp` | 由脚本生成（不再是全 0 占位） |
| `Desktop Sticker/Desktop Sticker.sln` | 加入 WallpaperStudio 工程 |
| `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/WallPaperPackage.h` | 解析新增可选字段 `content_flags` |
| Desktop Sticker 的壁纸设置页（`SettingsController` 的壁纸页 + `WallPaperStore`） | 展示 `requested_permissions` 与脚本徽标 |
| `tools/rebuild_and_test.ps1` | 加一步 Studio 无头测试（`-NoStudio` 跳过） |
| `AGENTS.md` | 补 Studio 的构建/运行/测试与脆弱点 |

---

## 阶段 0 · 加密与权限声明

### Task 1: 密钥单一定义并烧进 fork

**Files:**
- Create: `tools/wallpaper-studio/signing_key.txt`
- Modify: `tools/build_godot.ps1`
- Modify（由脚本生成）: `tools/godot-src/core/script_encryption_key.gen.cpp`

- [ ] **Step 1: 写测试——密钥文件合法且非全 0**

创建 `tools/wallpaper-studio/verify_key.ps1`：

```powershell
# 校验 signing_key.txt：必须是 64 位十六进制且不能是全 0（全 0 在引擎里表示"未启用加密"）。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/verify_key.ps1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$keyFile = Join-Path $PSScriptRoot 'signing_key.txt'
if (-not (Test-Path $keyFile)) { throw "missing $keyFile" }

$key = (Get-Content -Raw $keyFile).Trim()
if ($key.Length -ne 64) { throw "signing_key.txt must be 64 hex chars, got $($key.Length)" }
if ($key -notmatch '^[0-9a-fA-F]{64}$') { throw 'signing_key.txt must be hexadecimal' }
if ($key -match '^0{64}$') { throw 'signing_key.txt must not be all zeros (engine treats that as "no encryption")' }

Write-Host "[ok] signing_key.txt is a valid 256-bit key"
```

- [ ] **Step 2: 运行，确认它失败**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/verify_key.ps1
```

Expected: `missing .../signing_key.txt`（文件还不存在），非 0 退出。

- [ ] **Step 3: 生成密钥文件**

生成一个不可预测的 256 位密钥并落盘（只做一次；此后它就是仓库的固定身份，**不要重新生成**）：

```bash
powershell -NoProfile -Command "$b=New-Object byte[] 32; [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); ($b | ForEach-Object { $_.ToString('x2') }) -join '' | Set-Content -NoNewline -Encoding ascii 'tools/wallpaper-studio/signing_key.txt'"
```

- [ ] **Step 4: 运行，确认它通过**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/verify_key.ps1
```

Expected: `[ok] signing_key.txt is a valid 256-bit key`，退出码 0。

- [ ] **Step 5: 让 `build_godot.ps1` 在构建前烧入**

在 `tools/build_godot.ps1` 里，紧接在 `$SrcDir` 定义之后（`$pyExe` 解析之前）插入：

```powershell
# 把 signing_key.txt 烧进引擎：生成 core/script_encryption_key.gen.cpp。
# 引擎里 script_encryption_key 是编译期常量（core/object/script_language.h 的 extern，
# 实现在 core/script_encryption_key.gen.cpp），非编辑器构建解密 PCK 时只能用它。
# 全 0 表示"未启用加密"，所以这里拒绝全 0。
$studioKeyFile = Join-Path $ToolsDir 'wallpaper-studio\signing_key.txt'
if (-not (Test-Path $studioKeyFile)) {
    throw "missing $studioKeyFile - Studio 加密导出需要它（见 tools/wallpaper-studio/README.md）"
}
$studioKey = (Get-Content -Raw $studioKeyFile).Trim()
if ($studioKey -notmatch '^[0-9a-fA-F]{64}$' -or $studioKey -match '^0{64}$') {
    throw "$studioKeyFile must be 64 hex chars and not all zeros"
}

$keyBytes = @()
for ($i = 0; $i -lt 64; $i += 2) {
    $keyBytes += [Convert]::ToInt32($studioKey.Substring($i, 2), 16)
}
$keyLines = @()
for ($i = 0; $i -lt 32; $i += 10) {
    $chunk = $keyBytes[$i..([Math]::Min($i + 9, 31))]
    $keyLines += "`t" + (($chunk | ForEach-Object { $_.ToString() }) -join ', ') + ','
}
$genPath = Join-Path $SrcDir 'core\script_encryption_key.gen.cpp'
$genBody = @"
/* THIS FILE IS GENERATED BY tools/build_godot.ps1 - EDITS WILL BE LOST. */

#include <cstdint>

uint8_t script_encryption_key[32] = {
$($keyLines -join "`n")
};
"@
Set-Content -Path $genPath -Value $genBody -Encoding ascii -NoNewline
Write-Host "[key] script_encryption_key.gen.cpp written from wallpaper-studio/signing_key.txt"
```

- [ ] **Step 6: 只验证生成逻辑，不做全量构建**

```bash
powershell -NoProfile -Command "& { $s = Get-Content -Raw tools/wallpaper-studio/signing_key.txt; Write-Host \"key length: $($s.Trim().Length)\" }"
grep -c "0," tools/godot-src/core/script_encryption_key.gen.cpp
```

Expected: `key length: 64`，且 `grep` 计数不再是旧的全 0 内容（此时文件尚未被重写，计数为 30 —— 这是**预期**的，真正的写入发生在下次 `build_godot.ps1` 运行）。

- [ ] **Step 7: 提交**

```bash
git add tools/wallpaper-studio/signing_key.txt tools/wallpaper-studio/verify_key.ps1 tools/build_godot.ps1
git commit -m "feat(wallpaper): bake the wallpaper pack signing key into the godot fork"
```

> **注意**：`signing_key.txt` 是密钥，但它是**分发密钥**（要随 DLL 一起给用户），必须入库；
> 它不是服务端凭据。这一点在 `tools/wallpaper-studio/README.md` 里写清楚。

- [ ] **Step 8: 构建运行库（长，可后台）**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1
```

Expected: 结束时 `[key] script_encryption_key.gen.cpp written …`，然后编译通过，
`tools/godot/godot.windows.template_release.x86_64.dll` 时间戳更新。

- [ ] **Step 9: 确认密钥确实进了二进制**

```bash
python -c "
key = open('tools/wallpaper-studio/signing_key.txt').read().strip()
data = open('tools/godot/godot.windows.template_release.x86_64.dll','rb').read()
print('key bytes present:', bytes.fromhex(key) in data)
"
```

Expected: `key bytes present: True`。

---

### Task 2: 加密打包工具（headless 编辑器）

**Files:**
- Create: `tools/wallpaper-studio/pack_encrypted.gd`
- Create: `tools/wallpaper-studio/pack_encrypted.ps1`

- [ ] **Step 1: 写打包脚本**

创建 `tools/wallpaper-studio/pack_encrypted.gd`。这是给**编辑器** headless 跑的
（`--script`），所以可以用编辑器构建的完整脚本能力：

```gdscript
extends SceneTree
## 把任意目录打包成加密 PCK。用法（由 pack_encrypted.ps1 调用）：
##   godot.windows.editor.x86_64.exe --headless --path <任一个有效工程> \
##       --script res://pack_encrypted.gd -- --source <目录> --out <pck> --key <64hex>
##
## 这是阶段 0 的验证工具，也是"密钥 + PCKPacker 加密"这条链路的可执行参考；
## 阶段 1 之后 Studio 自己用 src/export/packer.gd 做同样的事。

func _initialize() -> void:
	var args := _user_args()
	var source: String = args.get("source", "")
	var out: String = args.get("out", "")
	var key: String = args.get("key", "")
	if source.is_empty() or out.is_empty() or key.length() != 64:
		push_error("usage: --source <dir> --out <pck> --key <64hex>")
		quit(2)
		return

	var files := _collect(source)
	if files.is_empty():
		push_error("no files under " + source)
		quit(3)
		return

	var packer := PCKPacker.new()
	if packer.pck_start(out, 32, key, true) != OK:
		push_error("pck_start failed: " + out)
		quit(4)
		return

	var written := 0
	for f in files:
		var rel: String = f.substr(source.length()).replace("\\", "/").trim_prefix("/")
		var target := "res://" + rel
		if packer.add_file(target, f, true) != OK:
			push_error("add_file failed: " + target)
			quit(5)
			return
		written += 1
	if packer.flush() != OK:
		push_error("flush failed")
		quit(6)
		return

	print("[pack] %d files -> %s (encrypted, directory index encrypted)" % [written, out])
	quit(0)

func _user_args() -> Dictionary:
	var out := {}
	var raw := OS.get_cmdline_user_args()
	for i in range(0, raw.size() - 1, 2):
		out[String(raw[i]).trim_prefix("--")] = raw[i + 1]
	return out

func _collect(dir_path: String) -> PackedStringArray:
	var out := PackedStringArray()
	var stack := [dir_path]
	while not stack.is_empty():
		var d: String = stack.pop_back()
		var da := DirAccess.open(d)
		if da == null:
			continue
		da.list_dir_begin()
		var name := da.get_next()
		while name != "":
			if name != "." and name != "..":
				var full := d.path_join(name)
				if da.current_is_dir():
					stack.push_back(full)
				else:
					out.append(full.replace("\\", "/"))
			name = da.get_next()
	return out
```

- [ ] **Step 2: 写调用壳**

创建 `tools/wallpaper-studio/pack_encrypted.ps1`：

```powershell
# 用 fork 出来的 Godot 编辑器 headless 打包一个加密 PCK。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/pack_encrypted.ps1 `
#           -Source <目录> -Out <pck 路径>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Editor = Join-Path $RepoRoot 'tools\godot-src\bin\godot.windows.editor.x86_64.console.exe'
$ToolGd = Join-Path $RepoRoot 'tools\wallpaper-studio\pack_encrypted.gd'
$KeyFile = Join-Path $PSScriptRoot 'signing_key.txt'

if (-not (Test-Path $Editor)) { throw "editor binary missing: $Editor (run tools/build_godot.ps1 -Target Editor)" }
if (-not (Test-Path $KeyFile)) { throw "missing $KeyFile" }
$Key = (Get-Content -Raw $KeyFile).Trim()

# --script 需要 res:// 下的路径，所以把工具脚本临时拷进一个最小工程。
$Scratch = Join-Path $env:TEMP ('dstk_pack_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Scratch | Out-Null
try {
    Set-Content -Path (Join-Path $Scratch 'project.godot') -Value "config_version=5`n`n[application]`nconfig/name=`"dstk_pack`"`n" -Encoding utf8
    Copy-Item $ToolGd (Join-Path $Scratch 'pack_encrypted.gd')

    # -Source 必须是绝对路径：引擎里的 --source 只是个字符串，我们用文件系统 API 读它。
    $SourceAbs = (Resolve-Path $Source).Path
    $OutAbs = [System.IO.Path]::GetFullPath($Out)

    & $Editor --headless --path $Scratch --script res://pack_encrypted.gd -- --source $SourceAbs --out $OutAbs --key $Key
    if ($LASTEXITCODE -ne 0) { throw "pack failed (exit $LASTEXITCODE)" }
} finally {
    Remove-Item -Recurse -Force $Scratch -ErrorAction SilentlyContinue
}
Write-Host "[done] $Out"
```

> `pack_encrypted.ps1` 含中文注释，必须存成 **UTF-8 with BOM**，否则 PowerShell 按 GBK 解析会炸。

- [ ] **Step 3: 打包现有模板工程，确认产物是密文**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/pack_encrypted.ps1 \
  -Source "D:/project/Desktop Sticker/tools/godot-wallpaper-template" \
  -Out    "D:/project/Desktop Sticker/build_tmp/template_enc.pck"
python -c "
d=open('build_tmp/template_enc.pck','rb').read()
print('size', len(d))
print('starts with GDPC:', d[:4] == b'GDPC')
print('plaintext scene name leak:', b'Wallpaper2D' in d)
"
```

Expected: `starts with GDPC: True`，`plaintext scene name leak: False`
（目录索引被加密，场景名不应以明文出现在包里）。

- [ ] **Step 4: 提交**

```bash
git add tools/wallpaper-studio/pack_encrypted.gd tools/wallpaper-studio/pack_encrypted.ps1
git commit -m "feat(wallpaper): encrypted pck packer tool for the studio pipeline"
```

---

### Task 3: 端到端验证加密包能被运行库加载

**Files:** 无新增（用现有 `tools/godot-host-test/`）

- [ ] **Step 1: 用运行库加载刚打的加密包**

```bash
cd "D:/project/Desktop Sticker/tools/godot-host-test"
./dstk-godot-host.exe --pack "D:/project/Desktop Sticker/build_tmp/template_enc.pck" --top --auto-exit 8
```

Expected: 打出 `[host] instance started`，8 秒后 `[host] auto-exit after 8.0s`，
**不出现** `Can't open encrypted pack` / `Condition "err" is true` 之类的错误。

- [ ] **Step 2: 反证——全 0 密钥下必须失败**

把 `tools/godot-src/core/script_encryption_key.gen.cpp` 临时改回全 0、只重编 runtime，
再跑一次 Step 1，**必须**报错。这一步确认"我们看到的成功是真的解密了"，不是"根本没加密"。

```bash
# 用 git 恢复全 0 版本，重编 runtime（仅这一次，为反证）
cd "D:/project/Desktop Sticker"
cp tools/godot-src/core/script_encryption_key.gen.cpp /tmp/key_backup.cpp
git show HEAD~1:tools/godot-src/core/script_encryption_key.gen.cpp > tools/godot-src/core/script_encryption_key.gen.cpp
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1
cd tools/godot-host-test && ./dstk-godot-host.exe --pack ".../template_enc.pck" --top --auto-exit 5
# 恢复
cd "D:/project/Desktop Sticker" && cp /tmp/key_backup.cpp tools/godot-src/core/script_encryption_key.gen.cpp
```

Expected: 反证运行时控制台出现解密失败相关的报错，进程不会正常渲染。

- [ ] **Step 3: 恢复并重建**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1
```

- [ ] **Step 4: 在 `tools/wallpaper-studio/README.md` 里记录这条验证**

创建 `tools/wallpaper-studio/README.md`，写清楚：密钥的作用、为什么入库、如何反证、
以及"密钥可被提取，加密只防随手翻看"这一诚实声明（照抄 spec §6.1 的措辞）。

- [ ] **Step 5: 提交**

```bash
git add tools/wallpaper-studio/README.md
git commit -m "docs(wallpaper): document the studio signing key and its limits"
```

---

### Task 4: `content_flags` 与桌面贴纸侧的权限展示

**Files:**
- Modify: `Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/WallPaperPackage.h`
- Modify: `Desktop Sticker/DesktopSticker.Tests/`（新增用例）
- Modify: 壁纸设置页

- [ ] **Step 1: 写失败测试**

在 `DesktopSticker.Tests` 的壁纸包用例文件里（与现有 `parse_wallpaper_manifest` 用例同文件）
追加，风格照 `test_framework.h` 的 `dtest`：

```cpp
D_TEST(wallpaper_manifest_content_flags_parsed) {
    const std::string text = R"({
      "format_version": 1,
      "title": "带脚本的壁纸",
      "main_scene": "res://build/x/main.tscn",
      "content_flags": ["scripts"],
      "requested_permissions": ["network", "exec"]
    })";
    desktopsticker::wallpaper::WallPaperManifest m;
    std::string err;
    D_CHECK(desktopsticker::wallpaper::parse_wallpaper_manifest(text, m, err));
    D_CHECK_EQ(m.contentFlags.size(), std::size_t{1});
    D_CHECK_EQ(m.contentFlags[0], std::string{"scripts"});
    D_CHECK(desktopsticker::wallpaper::has_scripts(m));
}

D_TEST(wallpaper_manifest_missing_content_flags_is_empty) {
    const std::string text = R"({
      "format_version": 1, "main_scene": "res://m.tscn"
    })";
    desktopsticker::wallpaper::WallPaperManifest m;
    std::string err;
    D_CHECK(desktopsticker::wallpaper::parse_wallpaper_manifest(text, m, err));
    D_CHECK(m.contentFlags.empty());
    D_CHECK(!desktopsticker::wallpaper::has_scripts(m));
}
```

- [ ] **Step 2: 跑测试，确认编译失败**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev_build.ps1 -Filter wallpaper_manifest
```

Expected: 编译错误 `contentFlags` 不是成员 / `has_scripts` 未声明。

- [ ] **Step 3: 实现**

在 `WallPaperPackage.h` 的 `WallPaperManifest` 里，`requestedPermissions` 之后加：

```cpp
    std::vector<std::string> contentFlags; // 当前只定义 "scripts"：包内含用户脚本
```

在 `read_string_array(j, "requested_permissions", out.requestedPermissions);` 之后加：

```cpp
    read_string_array(j, "content_flags", out.contentFlags);
```

在文件末尾、`has_wallpaper_pack_entry` 之前加：

```cpp
// 包里是否含用户脚本。用于设置页的醒目提示：脚本会在桌面贴纸进程内运行。
// 缺失 content_flags 时返回 false —— 旧包按"不含脚本"处理，不因缺字段报警。
inline bool has_scripts(const WallPaperManifest& m) {
    for (const auto& f : m.contentFlags) {
        if (f == "scripts") return true;
    }
    return false;
}
```

- [ ] **Step 4: 跑测试**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev_build.ps1 -Filter wallpaper_manifest
```

Expected: 两个用例通过，总数从 230 变为 232。

- [ ] **Step 5: 设置页展示**

在壁纸设置页里，条目详情处（`SettingsController` 的壁纸页）：

- 若 `requestedPermissions` 非空 → 逐项显示中文标签：
  `network`→网络访问、`file_outside`→包外文件读写、`exec`→执行外部程序、`system`→系统访问；
- 若 `has_scripts` → 条目列表上打一个黄色徽标 `脚本`，详情页顶部加一条警示：
  **「此壁纸包含脚本，会在桌面贴纸进程内运行。桌面贴纸当前不会限制它的能力。」**
- 措辞必须与上述一致：**不能**暗示已经做了限制。

- [ ] **Step 6: 人工验收**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File build.bat
# 手动：用 pack_encrypted.ps1 打一个包 → 在设置页导入 → 确认权限项与脚本徽标出现
```

- [ ] **Step 7: 提交**

```bash
git add "Desktop Sticker/DesktopSticker.WallPaper/include/desktopsticker/wallpaper/WallPaperPackage.h" \
        "Desktop Sticker/DesktopSticker.Tests" "Desktop Sticker/Desktop Sticker/SettingsController.cpp"
git commit -m "feat(wallpaper): surface pack permissions and script warnings in settings"
```

---

## 阶段 1 · 编辑器骨架与导出闭环

### Task 5: Studio 宿主 EXE

**Files:**
- Create: `Desktop Sticker/DesktopSticker.WallpaperStudio/DesktopSticker.WallpaperStudio.vcxproj`
- Create: `Desktop Sticker/DesktopSticker.WallpaperStudio/main.cpp`
- Modify: `Desktop Sticker/Desktop Sticker.sln`

- [ ] **Step 1: 写宿主**

创建 `main.cpp`。要点：**不**调用 `dstk_embed_set_present_hwnd`，让引擎自建顶层窗口。

```cpp
// DesktopSticker Wallpaper Studio —— 宿主进程。
//
// 职责刻意保持极小：加载 fork 出的 libgodot、把 studio 工程交给它、驱动迭代。
// 编辑器的一切（界面、文档、导出）都在 <exeDir>\studio\ 那个 Godot 工程里。
//
// 与 godot-host-test 的关键区别：这里**不**设置外部呈现 HWND，引擎自建顶层窗口 ——
// Studio 是一个普通桌面应用，不是挂在 WorkerW 上的壁纸。
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using CreateFn = void *(*)(int, char **);
using BoolFn = bool (*)(void *);
using VoidFn = void (*)(void *);

std::wstring exe_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

bool file_exists(const std::wstring &p) {
    const DWORD attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void fatal(const std::wstring &message) {
    MessageBoxW(nullptr, message.c_str(), L"桌面贴纸 · 壁纸编辑器", MB_ICONERROR | MB_OK);
    std::fwprintf(stderr, L"[studio] FATAL: %s\n", message.c_str());
}

std::string to_utf8(const std::wstring &s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

// --self-test：加载 DLL、创建实例、跑 3 帧、退出。给构建脚本做冒烟用。
bool run_self_test(CreateFn create, BoolFn start, BoolFn iteration, VoidFn destroy,
                   const std::wstring &studio_dir) {
    std::string path_arg = "--path";
    std::string dir_arg = to_utf8(studio_dir);
    std::string quit_arg = "--quit-after";
    std::string frames_arg = "3";
    std::vector<char *> argv{
        const_cast<char *>("dstk-studio.exe"),
        path_arg.data(), dir_arg.data(),
        quit_arg.data(), frames_arg.data(),
    };
    void *instance = create(static_cast<int>(argv.size()), argv.data());
    if (!instance) { std::printf("[studio] self-test: create failed\n"); return false; }
    if (!start(instance)) { destroy(instance); std::printf("[studio] self-test: start failed\n"); return false; }
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        if (!iteration(instance)) { ok = false; break; }
    }
    destroy(instance);
    std::printf("[studio] self-test: %s\n", ok ? "ok" : "iteration stopped early");
    return ok;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    const std::wstring dir = exe_dir();
    const std::wstring dll_path = dir + L"\\godot\\godot.windows.template_release.x86_64.dll";
    const std::wstring studio_dir = dir + L"\\studio";

    bool self_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--self-test") == 0) self_test = true;
    }

    // 单实例：二次启动直接把已有窗口带到前台（用命名互斥体 + 广播自定义消息）。
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"DesktopSticker.WallpaperStudio.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"DesktopSticker.Studio.Activate"), 0, 0);
        return 0;
    }

    if (!file_exists(dll_path)) {
        fatal(L"找不到 Godot 运行库：\n" + dll_path +
              L"\n\n请先运行 tools\\build_godot.ps1。");
        return 3;
    }
    if (!file_exists(studio_dir + L"\\project.godot")) {
        fatal(L"找不到编辑器工程：\n" + studio_dir +
              L"\n\n请重新构建（工程的 PostBuildEvent 会拷贝 tools\\wallpaper-studio\\project）。");
        return 3;
    }

    HMODULE dll = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) {
        fatal(L"加载 Godot 运行库失败（err=" + std::to_wstring(GetLastError()) + L"）：\n" + dll_path);
        return 3;
    }

    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "dstk_godot_create"));
    auto start = reinterpret_cast<BoolFn>(GetProcAddress(dll, "dstk_godot_start"));
    auto iteration = reinterpret_cast<BoolFn>(GetProcAddress(dll, "dstk_godot_iteration"));
    auto destroy = reinterpret_cast<VoidFn>(GetProcAddress(dll, "dstk_godot_destroy"));
    if (!create || !start || !iteration || !destroy) {
        fatal(L"运行库缺少 dstk_* 导出。DLL 与 Studio 不是同一次构建的产物。");
        return 3;
    }

    if (self_test) {
        return run_self_test(create, start, iteration, destroy, studio_dir) ? 0 : 1;
    }

    std::string path_arg = "--path";
    std::string dir_arg = to_utf8(studio_dir);
    std::vector<char *> engine_argv{
        const_cast<char *>("dstk-studio.exe"),
        path_arg.data(), dir_arg.data(),
    };

    void *instance = create(static_cast<int>(engine_argv.size()), engine_argv.data());
    if (!instance) { fatal(L"Godot 实例创建失败。"); return 1; }
    if (!start(instance)) { destroy(instance); fatal(L"Godot 实例启动失败。"); return 1; }

    // 引擎自己管消息循环与窗口；iteration 返回 false 表示窗口被关闭或要求退出。
    while (iteration(instance)) {
    }
    destroy(instance);

    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
```

- [ ] **Step 2: 建工程文件**

创建 `DesktopSticker.WallpaperStudio.vcxproj`。照 `DesktopSticker.Tests` 的结构，
关键差异：`ConfigurationType=Application`、`<SubSystem>Console</SubSystem>`、
入口是 `wmain`（所以 `CharacterSet=Unicode`），无 PrecompiledHeader。

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{55555555-6666-7777-8888-999999999999}</ProjectGuid>
    <RootNamespace>DesktopStickerWallpaperStudio</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <!-- 与 Desktop Sticker 主工程同一个输出目录：Studio 要和壁纸 DLL、godot\ 并列 -->
    <OutDir>$(SolutionDir)x64\$(Configuration)\Desktop Sticker\</OutDir>
    <IntDir>$(SolutionDir)obj\$(Configuration)\$(ProjectName)\</IntDir>
    <TargetName>dstk-studio</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <WarningLevel>Level4</WarningLevel>
      <PreprocessorDefinitions>WIN32_LEAN_AND_MEAN;NOMINMAX;UNICODE;_UNICODE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
      <AdditionalDependencies>user32.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <PostBuildEvent>
      <!-- 编辑器工程按目录分发（导出流程需要可写的 res://），见 spec §2.2 -->
      <Command>xcopy /Y /E /I "$(SolutionDir)..\tools\wallpaper-studio\project" "$(OutDir)studio\"
if exist "$(SolutionDir)..\tools\godot\" xcopy /Y /D /E "$(SolutionDir)..\tools\godot\*" "$(OutDir)godot\"
xcopy /Y /D "$(SolutionDir)..\tools\wallpaper-studio\signing_key.txt" "$(OutDir)studio\signing_key.txt"</Command>
    </PostBuildEvent>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="main.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
```

> `signing_key.txt` 被拷进 `studio/`，Studio 运行时读它来加密导出。它已经在源码树里，
> 拷贝产物要被 gitignore（`studio/` 下的拷贝落在 `x64/`，本来就 gitignore，无需额外处理）。

- [ ] **Step 3: 加进解决方案**

在 `Desktop Sticker.sln` 的 `Project(...)` 段落末尾追加（照现有工程行的格式）：

```
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "DesktopSticker.WallpaperStudio", "DesktopSticker.WallpaperStudio\DesktopSticker.WallpaperStudio.vcxproj", "{55555555-6666-7777-8888-999999999999}"
EndProject
```

并在两个 `GlobalSection(...)`（`ProjectConfigurationPlatforms` 与 `NestedProjects` 如存在）
里补上对应的 `Release|x64` 行，照现有工程的写法复制改名。

- [ ] **Step 4: 建一个最小的 studio 工程，让宿主能起来**

创建 `tools/wallpaper-studio/project/project.godot`：

```ini
; Studio 的 Godot 工程配置。这里刻意不放任何 Editor 相关设置 —— Studio 跑的是
; template_release 运行库，不是编辑器。
config_version=5

[application]

config/name="桌面贴纸 · 壁纸编辑器"
run/main_scene="res://main.tscn"
config/features=PackedStringArray("4.7")

[display]

window/size/viewport_width=1440
window/size/viewport_height=900
window/size/resizable=true

[rendering]

renderer/rendering_method="forward_plus"
```

创建 `tools/wallpaper-studio/project/main.tscn`（阶段 1 先只有一个占位 Label，
Task 11–13 再填内容）：

```ini
[gd_scene format=3]

[node name="Studio" type="Control"]
layout_mode = 3
anchors_preset = 15
anchor_right = 1.0
anchor_bottom = 1.0

[node name="Placeholder" type="Label" parent="."]
layout_mode = 1
anchors_preset = 8
anchor_left = 0.5
anchor_top = 0.5
anchor_right = 0.5
anchor_bottom = 0.5
offset_left = -160.0
offset_top = -16.0
offset_right = 160.0
offset_bottom = 16.0
text = "壁纸编辑器骨架"
horizontal_alignment = 1
vertical_alignment = 1
```

- [ ] **Step 5: 构建并冒烟**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File build.bat
"D:/project/Desktop Sticker/Desktop Sticker/x64/Release/Desktop Sticker/dstk-studio.exe" --self-test
```

Expected: `[studio] self-test: ok`，退出码 0。

- [ ] **Step 6: 手动跑一次**

```bash
"D:/project/Desktop Sticker/Desktop Sticker/x64/Release/Desktop Sticker/dstk-studio.exe"
```

Expected: 出现一个标题为「桌面贴纸 · 壁纸编辑器」的窗口，中间显示「壁纸编辑器骨架」，
关掉窗口后进程退出。

- [ ] **Step 7: 提交**

```bash
git add "Desktop Sticker/DesktopSticker.WallpaperStudio" "Desktop Sticker/Desktop Sticker.sln" \
        tools/wallpaper-studio/project
git commit -m "feat(studio): libgodot host exe and the editor project skeleton"
```

---

### Task 6: 无头测试运行器

**Files:**
- Create: `tools/wallpaper-studio/project/tests/test_framework.gd`
- Create: `tools/wallpaper-studio/project/tests/run_tests.gd`
- Create: `tools/wallpaper-studio/run_tests.ps1`

- [ ] **Step 1: 写框架**

`tests/test_framework.gd`（约 60 行，不引第三方）：

```gdscript
class_name DTest
## 极简测试框架：用例注册 + 断言 + 汇总。
## 刻意不引 GUT/GdUnit：Studio 的测试要能在纯运行库下无头跑，依赖越少越好。
## 用法见 tests/run_tests.gd。

static var _cases: Array = []          # [{name, fn}]
static var _failures: Array = []       # ["case: message"]
static var _current: String = ""

## 必须独占一个进程的用例：ProjectSettings.load_resource_pack 一旦挂载就不能卸载。
## 跑全量时跳过，由 run_tests.ps1 用 -Only 单跑。
const EXCLUSIVE_CASES := ["exporter_produces_loadable_pack"]

static func case(case_name: String, fn: Callable) -> void:
	_cases.append({ "name": case_name, "fn": fn })

static func run_all(only: String) -> int:
	var ran := 0
	for c in _cases:
		if only != "":
			if c.name != only:
				continue
		elif c.name in EXCLUSIVE_CASES:
			continue
		_current = c.name
		ran += 1
		c.fn.call()
	var failed := _failures.size()
	for f in _failures:
		printerr("FAIL  ", f)
	print("tests: %d/%d cases run, %d failures" % [ran, _cases.size(), failed])
	return 1 if failed > 0 else 0

static func ok(cond: bool, what: String) -> void:
	if not cond:
		_failures.append("%s: %s" % [_current, what])

static func eq(actual, expected, what: String) -> void:
	if actual != expected:
		_failures.append("%s: %s (got %s, want %s)" % [_current, what, actual, expected])

static func approx(actual: float, expected: float, eps: float, what: String) -> void:
	if absf(actual - expected) > eps:
		_failures.append("%s: %s (got %f, want %f)" % [_current, what, actual, expected])
```

- [ ] **Step 2: 写入口**

`tests/run_tests.gd`：

```gdscript
extends SceneTree
## Studio 无头测试入口。
##   dstk-studio.exe --path <studio> --headless --script res://tests/run_tests.gd --quit-after 600
##   dstk-studio.exe ... -- --only=doc_roundtrip          # 只跑一个用例
##
## 退出码：0 全通过，1 有用例失败。
##
## 独占用例由 DTest.EXCLUSIVE_CASES 声明，run_all 会在跑全量时自动跳过。
func _initialize() -> void:
	# 每个测试文件在被 load 时用 DTest.case() 注册自己。
	var dir := DirAccess.open("res://tests")
	for f in dir.get_files():
		var name := f.trim_suffix(".remap")
		if name.begins_with("test_") and name.ends_with(".gd"):
			load("res://tests/" + name)

	var only := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--only="):
			only = a.trim_prefix("--only=")

	quit(DTest.run_all(only))
```

- [ ] **Step 3: 写调用壳**

`tools/wallpaper-studio/run_tests.ps1`（UTF-8 with BOM）：

```powershell
# Studio 无头测试。两阶段：先跑可同进程的用例，再单跑必须独占进程的用例。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1 [-Only <用例名>]
[CmdletBinding()]
param([string]$Only = '')
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$StudioExe = Join-Path $RepoRoot 'Desktop Sticker\x64\Release\Desktop Sticker\dstk-studio.exe'
$Project = Join-Path $RepoRoot 'Desktop Sticker\x64\Release\Desktop Sticker\studio'

if (-not (Test-Path $StudioExe)) { throw "missing $StudioExe - run build.bat first" }
if (-not (Test-Path (Join-Path $Project 'project.godot'))) { throw "missing studio project at $Project" }

function Invoke-StudioTests([string]$only) {
    $extra = @()
    if ($only -ne '') { $extra = @('--', "--only=$only") }
    & $StudioExe --path $Project --headless --script res://tests/run_tests.gd --quit-after 600 @extra
    return $LASTEXITCODE
}

if ($Only -ne '') { exit (Invoke-StudioTests $Only) }

$code = Invoke-StudioTests ''
if ($code -ne 0) { exit $code }

# 独占用例：必须各自一个新进程（load_resource_pack 不可卸载）
foreach ($case in @('exporter_produces_loadable_pack')) {
    $code = Invoke-StudioTests $case
    if ($code -ne 0) { exit $code }
}
exit 0
```

- [ ] **Step 4: 放一个必然通过的用例验证运行器**

`tests/test_paths.gd`（内容在 Task 7 补全，这里先放一个等价于冒烟的最小用例）：

```gdscript
extends RefCounted

static func _register() -> void:
	DTest.case("framework_works", func() -> void:
		DTest.ok(true, "true is true")
		DTest.eq(1 + 1, 2, "arithmetic")
	)

_register()
```

- [ ] **Step 5: 跑**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: 运行器输出 `tests: 1/1 cases run, 0 failures`，退出码 0。

- [ ] **Step 6: 故意弄坏，确认它以非 0 退出**

把 `DTest.eq(1 + 1, 2, ...)` 改成 `DTest.eq(1 + 1, 3, ...)`，重跑：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1; echo "exit=$?"
```

Expected: `FAIL  framework_works: arithmetic (got 2, want 3)`、`0/1 cases run, 1 failures`、`exit=1`。改回去。

- [ ] **Step 7: 提交**

```bash
git add tools/wallpaper-studio/project/tests tools/wallpaper-studio/run_tests.ps1
git commit -m "test(studio): headless gdscript test runner for the editor project"
```

---

### Task 7: 路径纯函数

**Files:**
- Create: `tools/wallpaper-studio/project/src/doc/paths.gd`
- Modify: `tools/wallpaper-studio/project/tests/test_paths.gd`

- [ ] **Step 1: 写失败测试**

`tests/test_paths.gd` 的内容换成：

```gdscript
extends RefCounted

static func _register() -> void:
	DTest.case("paths_build_dir_is_under_res", func() -> void:
		DTest.eq(StudioPaths.build_dir("abc123"), "res://build/abc123", "build dir")
	)
	DTest.case("paths_scratch_dir_under_user", func() -> void:
		DTest.eq(StudioPaths.scratch_dir("abc123").begins_with("user://"), true, "scratch under user://")
	)
	DTest.case("paths_new_pack_id_is_32_hex", func() -> void:
		var id := StudioPaths.new_pack_id()
		DTest.eq(id.length(), 32, "pack id length")
		DTest.ok(id.to_lower() == id and id.is_valid_hex_number(false), "pack id is lowercase hex")
	)
	DTest.case("paths_dswall_name_sanitized", func() -> void:
		DTest.eq(StudioPaths.dswall_file_name("我的/壁纸:"), "我的_壁纸_.dswall", "illegal chars replaced")
		DTest.eq(StudioPaths.dswall_file_name("  "), "wallpaper.dswall", "empty falls back")
	)

_register()
```

- [ ] **Step 2: 跑，确认失败**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: 4 个 FAIL，`Identifier "StudioPaths" not declared`。

- [ ] **Step 3: 实现**

创建 `src/doc/paths.gd`：

```gdscript
class_name StudioPaths
## Studio 用到的路径计算。全部是纯函数 —— 不读盘、不建目录，便于无头测试。

## 构建目录必须在 res:// 下：ResourceSaver 会把写在工程目录里的绝对路径自动本地化成
## res:// 相对路径，导出时 PCK 里的路径因此天然对上（spec §2.2）。所以 Studio 必须以
## 目录形式分发 —— 打包成一个 pck 时 res:// 是只读的，导出会直接失败。
static func build_dir(pack_id: String) -> String:
	return "res://build/" + pack_id

## 用户工程（.dswproj 目录）默认落在 user:// 下，跟安装目录分开。
static func default_project_root() -> String:
	return "user://projects"

## 时间轴/画布用的临时资源。
static func scratch_dir(pack_id: String) -> String:
	return "user://scratch/" + pack_id

## 16 字节随机十六进制。工程首次保存时生成一次，之后固定不变（热应用去重靠它）。
static func new_pack_id() -> String:
	var bytes := Crypto.new().generate_random_bytes(16)
	var s := ""
	for b in bytes:
		s += "%02x" % b
	return s

## Windows 文件名非法字符 -> "_"。空/全非法时回落 "wallpaper"。
static func dswall_file_name(title: String) -> String:
	var out := ""
	for i in title.length():
		var c := title[i]
		if c in ["\\", "/", ":", "*", "?", "\"", "<", ">", "|"]:
			out += "_"
		elif c.unicode_at(0) < 32:
			out += "_"
		else:
			out += c
	out = out.strip_edges()
	if out.is_empty():
		out = "wallpaper"
	return out + ".dswall"

## 收件箱：桌面贴纸的热应用入口（先写 .tmp 再改名，模块只认 .dswall）。
static func inbox_dir() -> String:
	var appdata := OS.get_environment("APPDATA")
	if appdata.is_empty():
		return ""
	return appdata.replace("\\", "/").path_join("DesktopSticker").path_join("wallpaper_inbox")
```

- [ ] **Step 4: 跑，确认通过**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: `tests: 4/4 cases run, 0 failures`。

- [ ] **Step 5: 提交**

```bash
git add tools/wallpaper-studio/project/src/doc/paths.gd tools/wallpaper-studio/project/tests/test_paths.gd
git commit -m "feat(studio): path helpers for build, scratch and inbox"
```

---

### Task 8: 文档模型

**Files:**
- Create: `tools/wallpaper-studio/project/src/doc/layer.gd`
- Create: `tools/wallpaper-studio/project/src/doc/doc.gd`
- Create: `tools/wallpaper-studio/project/tests/test_doc.gd`

- [ ] **Step 1: 写失败测试**

`tests/test_doc.gd`：

```gdscript
extends RefCounted

static func _register() -> void:
	DTest.case("doc_roundtrip_preserves_layers", func() -> void:
		var doc := StudioDoc.create_empty("测试", Vector2i(1920, 1080))
		var l := Layer.make_color("背景", Color(0.02, 0.07, 0.12))
		doc.add_layer(l)
		var text := doc.to_text()
		var parsed: Dictionary = StudioDoc.parse(text)
		DTest.eq(parsed.ok, true, "parse ok")
		DTest.eq(parsed.doc.name, "测试", "name kept")
		DTest.eq(parsed.doc.layers.size(), 1, "one layer")
		DTest.eq(parsed.doc.layers[0].type, "color", "layer type kept")
		DTest.eq(parsed.doc.layers[0].color, Color(0.02, 0.07, 0.12), "color kept")
	)
	DTest.case("doc_missing_fields_take_defaults", func() -> void:
		var parsed: Dictionary = StudioDoc.parse('{"format_version":1,"layers":[{"type":"color"}]}')
		DTest.eq(parsed.ok, true, "minimal doc parses")
		DTest.eq(parsed.doc.canvas, Vector2i(1920, 1080), "default canvas")
		DTest.eq(parsed.doc.layers[0].name, "图层", "default name")
		DTest.eq(parsed.doc.layers[0].transform.x, 0.0, "default transform")
	)
	DTest.case("doc_rejects_future_format_version", func() -> void:
		var parsed: Dictionary = StudioDoc.parse('{"format_version":99}')
		DTest.eq(parsed.ok, false, "future version rejected")
		DTest.ok(String(parsed.error).contains("99"), "error mentions the version")
	)
	DTest.case("doc_rejects_broken_json", func() -> void:
		DTest.eq(StudioDoc.parse("{not json").ok, false, "broken json rejected")
	)
	DTest.case("doc_layer_order_is_top_first", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		doc.add_layer(Layer.make_color("上", Color.WHITE))
		# 列表顶部 = 最上层；底层先渲染。
		DTest.eq(doc.layers[0].name, "底", "index 0 is the bottom layer")
		doc.move_layer(0, 1)
		DTest.eq(doc.layers[0].name, "上", "move_layer swaps")
	)
	DTest.case("doc_split_external_scripts", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_image("花瓣", "assets/petals.gif")
		l.script_path = "assets/myscript.gd"
		doc.add_layer(l)
		DTest.eq(doc.external_scripts(), ["assets/myscript.gd"], "external script listed")
	)

_register()
```

- [ ] **Step 2: 跑，确认失败**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -file tools/wallpaper-studio/run_tests.ps1
```

Expected: 编译错误 `StudioDoc` / `Layer` 未声明。

- [ ] **Step 3: 实现 `layer.gd`**

```gdscript
class_name Layer
## 单个图层。用 Dictionary 承载（不是 class），因为文档是 JSON 的真源，
## 保持"文档 <-> 字典"的转换零阻抗比类型安全更重要。

const TYPES := ["color", "image", "text", "particles"]
const BLENDS := ["mix", "add", "sub", "mul", "screen", "overlay"]
const CHANNELS := ["x", "y", "scale", "rotation", "opacity"]

static func _base(type: String, name: String) -> Dictionary:
	return {
		"id": "l_" + StudioPaths.new_pack_id().substr(0, 8),
		"type": type,
		"name": name,
		"visible": true,
		"transform": { "x": 0.0, "y": 0.0, "scale": 1.0, "rotation": 0.0, "opacity": 1.0 },
		"blend": "mix",
		"tracks": [],
		"effects": [],
		"script_path": "",
		"shader_path": "",
	}

static func make_color(name: String, color: Color) -> Dictionary:
	var l := _base("color", name)
	l["color"] = color
	return l

static func make_image(name: String, src: String) -> Dictionary:
	var l := _base("image", name)
	l["src"] = src
	l["fit"] = "contain"
	l["speed"] = 1.0
	return l

static func make_text(name: String, text: String) -> Dictionary:
	var l := _base("text", name)
	l["text"] = text
	l["font_size"] = 64
	l["color"] = Color(0.9, 0.95, 1.0)
	l["align"] = "center"
	l["dynamic"] = "none"
	l["date_format"] = "%04d-%02d-%02d"
	return l

static func make_particles(name: String, texture: String) -> Dictionary:
	var l := _base("particles", name)
	l["texture"] = texture
	l["amount"] = 120
	l["lifetime"] = 6.0
	l["direction"] = Vector2(0, -1)
	l["spread"] = 45.0
	l["velocity"] = 40.0
	l["gravity"] = Vector2(0, 20)
	l["scale_min"] = 0.4
	l["scale_max"] = 1.0
	return l

## 把字典里缺失/类型不对的字段补成默认值。纯函数，返回新字典。
static func normalize(raw: Dictionary) -> Dictionary:
	var type := String(raw.get("type", "color"))
	if not (type in TYPES):
		type = "color"
	var out := _base(type, String(raw.get("name", "图层")))
	out["id"] = String(raw.get("id", out["id"]))
	out["visible"] = bool(raw.get("visible", true))
	var blend := String(raw.get("blend", "mix"))
	out["blend"] = blend if blend in BLENDS else "mix"
	out["script_path"] = String(raw.get("script_path", ""))
	out["shader_path"] = String(raw.get("shader_path", ""))
	if raw.get("transform") is Dictionary:
		var t: Dictionary = raw["transform"]
		for k in ["x", "y", "rotation"]:
			out["transform"][k] = float(t.get(k, out["transform"][k]))
		for k in ["scale", "opacity"]:
			out["transform"][k] = float(t.get(k, out["transform"][k]))
	if raw.get("tracks") is Array:
		out["tracks"] = raw["tracks"]
	if raw.get("effects") is Array:
		out["effects"] = raw["effects"]
	if type == "color":
		out["color"] = _to_color(raw.get("color"), Color(0.02, 0.07, 0.12))
	elif type == "image":
		out["src"] = String(raw.get("src", ""))
		var fit := String(raw.get("fit", "contain"))
		out["fit"] = fit if fit in ["fill", "contain", "cover", "tile"] else "contain"
		out["speed"] = float(raw.get("speed", 1.0))
	elif type == "text":
		out["text"] = String(raw.get("text", "文本"))
		out["font_size"] = int(raw.get("font_size", 64))
		out["color"] = _to_color(raw.get("color"), Color(0.9, 0.95, 1.0))
		out["align"] = String(raw.get("align", "center"))
		out["dynamic"] = String(raw.get("dynamic", "none"))
		out["date_format"] = String(raw.get("date_format", "%04d-%02d-%02d"))
	elif type == "particles":
		out["texture"] = String(raw.get("texture", ""))
		out["amount"] = int(raw.get("amount", 120))
		out["lifetime"] = float(raw.get("lifetime", 6.0))
		out["direction"] = _to_vec2(raw.get("direction"), Vector2(0, -1))
		out["spread"] = float(raw.get("spread", 45.0))
		out["velocity"] = float(raw.get("velocity", 40.0))
		out["gravity"] = _to_vec2(raw.get("gravity"), Vector2(0, 20))
		out["scale_min"] = float(raw.get("scale_min", 0.4))
		out["scale_max"] = float(raw.get("scale_max", 1.0))
	return out

static func _to_color(v, fallback: Color) -> Color:
	if v is Color:
		return v
	if v is String:
		var s := v.trim_prefix("#")
		if s.length() == 6 and s.is_valid_hex_number(false):
			return Color.html(s)
	return fallback

static func _to_vec2(v, fallback: Vector2) -> Vector2:
	if v is Vector2:
		return v
	if v is Dictionary:
		return Vector2(float(v.get("x", fallback.x)), float(v.get("y", fallback.y)))
	return fallback
```

- [ ] **Step 4: 实现 `doc.gd`**

```gdscript
class_name StudioDoc
extends RefCounted
## 壁纸文档。JSON 是唯一真源；所有编辑都改这个对象，导出/预览从它派生。
## 加载策略：任何字段缺失或类型错误都取默认值，只有 format_version 无法识别才失败。

const FORMAT_VERSION := 1

var name: String = "未命名壁纸"
var canvas: Vector2i = Vector2i(1920, 1080)
var background: Color = Color(0.02, 0.07, 0.12)
var pack_id: String = ""
var layers: Array = []          # Array[Dictionary]，index 0 = 最底层

static func create_empty(title: String, size: Vector2i) -> StudioDoc:
	var d := StudioDoc.new()
	d.name = title
	d.canvas = size
	d.pack_id = StudioPaths.new_pack_id()
	return d

## 返回 {ok: bool, doc: StudioDoc|null, error: String}
static func parse(text: String) -> Dictionary:
	var json := JSON.new()
	if json.parse(text) != OK:
		return { "ok": false, "doc": null, "error": "JSON 解析失败：第 %d 行 %s" % [json.get_error_line(), json.get_error_message()] }

	var raw = json.data
	if not (raw is Dictionary):
		return { "ok": false, "doc": null, "error": "wallpaper.json 的顶层不是对象" }

	var version := int(raw.get("format_version", 0))
	if version < 1 or version > FORMAT_VERSION:
		return { "ok": false, "doc": null, "error": "不支持的文档版本 %d（本编辑器支持到 %d），请升级桌面贴纸" % [version, FORMAT_VERSION] }

	var d := StudioDoc.new()
	d.name = String(raw.get("name", "未命名壁纸"))
	d.pack_id = String(raw.get("pack_id", ""))
	if d.pack_id.is_empty():
		d.pack_id = StudioPaths.new_pack_id()
	if raw.get("canvas") is Dictionary:
		var c: Dictionary = raw["canvas"]
		d.canvas = Vector2i(int(c.get("width", 1920)), int(c.get("height", 1080)))
	d.background = Layer._to_color(raw.get("background"), Color(0.02, 0.07, 0.12))
	if d.canvas.x < 64 or d.canvas.y < 64:
		return { "ok": false, "doc": null, "error": "画布尺寸过小：%s" % d.canvas }

	if raw.get("layers") is Array:
		for item in raw["layers"]:
			if item is Dictionary:
				d.layers.append(Layer.normalize(item))

	return { "ok": true, "doc": d, "error": "" }

func to_text() -> String:
	var out := {
		"format_version": FORMAT_VERSION,
		"name": name,
		"pack_id": pack_id,
		"canvas": { "width": canvas.x, "height": canvas.y },
		"background": "#" + background.to_html(false),
		"layers": layers,
	}
	return JSON.stringify(out, "  ")

func add_layer(layer: Dictionary) -> void:
	layers.append(Layer.normalize(layer))

func remove_layer(index: int) -> void:
	if index >= 0 and index < layers.size():
		layers.remove_at(index)

func move_layer(from_index: int, to_index: int) -> void:
	if from_index < 0 or from_index >= layers.size() or to_index < 0 or to_index >= layers.size():
		return
	var item = layers[from_index]
	layers.remove_at(from_index)
	layers.insert(to_index, item)

## 导出时需要写进 manifest.content_flags 的东西：用户自己带来的脚本（不含 Studio 生成的时钟脚本）。
func external_scripts() -> Array:
	var out := []
	for l in layers:
		if String(l.get("script_path", "")) != "":
			out.append(String(l["script_path"]))
	return out

## 导出前校验。返回问题列表（中文，直接给用户看）。
## project_dir 用于检查引用到的资源是否存在。
func validate(project_dir: String) -> PackedStringArray:
	var problems := PackedStringArray()
	if name.strip_edges().is_empty():
		problems.append("壁纸名称为空。")
	if canvas.x < 64 or canvas.y < 64:
		problems.append("画布尺寸过小：%s。" % canvas)
	var visible := 0
	for i in layers.size():
		var l: Dictionary = layers[i]
		if not bool(l.get("visible", true)):
			continue
		visible += 1
		var type := String(l["type"])
		if type == "image":
			var src := String(l.get("src", ""))
			if src.is_empty():
				problems.append("图层「%s」没有选择图片。" % l["name"])
			elif not FileAccess.file_exists(project_dir.path_join(src)):
				problems.append("图层「%s」的图片不存在：%s" % [l["name"], src])
		elif type == "particles":
			var tex := String(l.get("texture", ""))
			if not tex.is_empty() and not FileAccess.file_exists(project_dir.path_join(tex)):
				problems.append("图层「%s」的粒子贴图不存在：%s" % [l["name"], tex])
		if int(l.get("amount", 0)) < 0:
			problems.append("图层「%s」的粒子数量为负。" % l["name"])
	if visible == 0:
		problems.append("没有任何可见图层，导出的会是一张纯色图。")
	return problems
```

- [ ] **Step 5: 跑，确认通过**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: `tests: 11/11 cases run, 0 failures`。

- [ ] **Step 6: 提交**

```bash
git add tools/wallpaper-studio/project/src/doc tools/wallpaper-studio/project/tests/test_doc.gd
git commit -m "feat(studio): wallpaper document model with defaults and validation"
```

---

### Task 9: 场景构造（预览与导出共用）

**Files:**
- Create: `tools/wallpaper-studio/project/src/build/layer_nodes.gd`
- Create: `tools/wallpaper-studio/project/src/build/scene_builder.gd`
- Create: `tools/wallpaper-studio/project/tests/test_scene_builder.gd`

- [ ] **Step 1: 写失败测试**

`tests/test_scene_builder.gd`：

```gdscript
extends RefCounted

static func _build(doc: StudioDoc) -> Node:
	var root := SceneBuilder.build(doc, "")
	return root

static func _register() -> void:
	DTest.case("builder_root_is_full_rect_control", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		var root := _build(doc)
		DTest.ok(root is Control, "root is a Control")
		DTest.eq(root.size, Vector2(1280, 720), "root sized to canvas")
		root.free()
	)
	DTest.case("builder_layer_order_matches_document", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		doc.add_layer(Layer.make_text("时钟", "00:00:00"))
		var root := _build(doc)
		var names := []
		for c in root.get_children():
			names.append(c.name)
		DTest.eq(names, ["layer_0", "layer_1"], "children follow document order (bottom first)")
		DTest.ok(root.get_child(0) is ColorRect, "color layer -> ColorRect")
		DTest.ok(root.get_child(1) is Label, "text layer -> Label")
		root.free()
	)
	DTest.case("builder_applies_transform_and_visibility", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("底", Color.BLACK)
		l["transform"]["x"] = 100.0
		l["transform"]["opacity"] = 0.5
		l["visible"] = false
		doc.add_layer(l)
		var root := _build(doc)
		var n: Control = root.get_child(0)
		DTest.eq(n.position, Vector2(100, 0), "position applied")
		DTest.approx(n.modulate.a, 0.5, 0.001, "opacity applied")
		DTest.eq(n.visible, false, "visibility applied")
		root.free()
	)
	DTest.case("builder_injects_clock_script_for_dynamic_text", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_text("时钟", "00:00:00")
		l["dynamic"] = "clock"
		doc.add_layer(l)
		var root := _build(doc)
		var label: Label = root.get_child(0)
		DTest.ok(label.get_script() != null, "dynamic text gets a script")
		DTest.eq(label.get("mode"), "clock", "clock mode set")
		root.free()
	)
	DTest.case("builder_skips_clock_script_for_static_text", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_text("标题", "你好"))
		var root := _build(doc)
		var label: Label = root.get_child(0)
		DTest.eq(label.get_script(), null, "static text has no script")
		DTest.eq(label.text, "你好", "static text kept")
		root.free()
	)
	DTest.case("builder_has_no_animation_player_without_tracks", func() -> void:
		var doc := StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var root := _build(doc)
		for c in root.get_children():
			DTest.ok(not (c is AnimationPlayer), "no AnimationPlayer when no tracks")
		root.free()
	)

_register()
```

- [ ] **Step 2: 跑，确认失败**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: `SceneBuilder` 未声明。

- [ ] **Step 3: 实现 `layer_nodes.gd`**

```gdscript
class_name LayerNodes
## 每种图层类型 -> 节点。只构造，不落盘（落盘是 exporter 的事）。

## 时钟/日期图层的运行时脚本。导出时由 exporter 把这个常量写进构建目录，
## 同时在预览时直接在内存里编译同一段代码 —— 两条路用的是**同一份源码**，
## 所以预览和导出不可能不一致。
const DSTK_CLOCK_SCRIPT := "
extends Label

@export var mode: String = \"clock\"
@export var date_format: String = \"%04d-%02d-%02d\"

func _ready() -> void:
	_refresh()

func _process(_delta: float) -> void:
	_refresh()

func _refresh() -> void:
	var now := Time.get_datetime_dict_from_system()
	if mode == \"date\":
		text = date_format % [now.year, now.month, now.day]
	else:
		text = \"%02d:%02d:%02d\" % [now.hour, now.minute, now.second]
"

## 返回一个以 layer 内容为准的节点。project_dir 用于加载图片资源。
static func build(layer: Dictionary, project_dir: String) -> Control:
	var type := String(layer.get("type", "color"))
	match type:
		"color":
			return _build_color(layer)
		"image":
			return _build_image(layer, project_dir)
		"text":
			return _build_text(layer, project_dir)
		"particles":
			return _build_particles(layer, project_dir)
	return _build_color(layer)

static func _build_color(layer: Dictionary) -> Control:
	var n := ColorRect.new()
	n.color = Layer._to_color(layer.get("color"), Color.BLACK)
	n.set_anchors_preset(Control.PRESET_FULL_RECT)
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return n

static func _build_image(layer: Dictionary, project_dir: String) -> Control:
	var n := TextureRect.new()
	n.set_anchors_preset(Control.PRESET_FULL_RECT)
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var fit := String(layer.get("fit", "contain"))
	match fit:
		"fill":
			n.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			n.stretch_mode = TextureRect.STRETCH_SCALE
		"cover":
			n.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			n.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
		"tile":
			n.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			n.stretch_mode = TextureRect.STRETCH_TILE
		_:
			n.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			n.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	var tex := _load_texture(String(layer.get("src", "")), project_dir)
	n.texture = tex
	# GIF/WebP 动图：AnimatedTexture 在 4.x 不支持逐帧，改用 SpriteFrames 需要导入流程，
	# 所以统一走「第一帧静态」；真正的动图在阶段 2 落地（spec §9）。
	return n

static func _build_text(layer: Dictionary, _project_dir: String) -> Control:
	var n := Label.new()
	n.set_anchors_preset(Control.PRESET_FULL_RECT)
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	n.text = String(layer.get("text", ""))
	n.add_theme_font_size_override("font_size", int(layer.get("font_size", 64)))
	n.add_theme_color_override("font_color", Layer._to_color(layer.get("color"), Color.WHITE))
	match String(layer.get("align", "center")):
		"left":
			n.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
		"right":
			n.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		_:
			n.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	n.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	var dynamic := String(layer.get("dynamic", "none"))
	if dynamic != "none":
		var script := GDScript.new()
		script.source_code = DSTK_CLOCK_SCRIPT
		if script.reload() == OK:
			n.set_script(script)
			n.set("mode", dynamic)
			n.set("date_format", String(layer.get("date_format", "%04d-%02d-%02d")))
	return n

static func _build_particles(layer: Dictionary, project_dir: String) -> Control:
	var n := GPUParticles2D.new()
	n.amount = int(layer.get("amount", 120))
	n.lifetime = float(layer.get("lifetime", 6.0))
	n.emitting = true
	n.texture = _load_texture(String(layer.get("texture", "")), project_dir)
	var mat := ParticleProcessMaterial.new()
	mat.direction = Vector3(Layer._to_vec2(layer.get("direction"), Vector2(0, -1)).x,
			Layer._to_vec2(layer.get("direction"), Vector2(0, -1)).y, 0.0)
	mat.spread = float(layer.get("spread", 45.0))
	mat.initial_velocity_min = float(layer.get("velocity", 40.0)) * 0.8
	mat.initial_velocity_max = float(layer.get("velocity", 40.0))
	var g := Layer._to_vec2(layer.get("gravity"), Vector2(0, 20))
	mat.gravity = Vector3(g.x, g.y, 0.0)
	mat.scale_min = float(layer.get("scale_min", 0.4))
	mat.scale_max = float(layer.get("scale_max", 1.0))
	n.process_material = mat
	# GPUParticles2D 是 Node2D，不是 Control；用一个铺满的 Control 包住以统一布局。
	var wrap := Control.new()
	wrap.set_anchors_preset(Control.PRESET_FULL_RECT)
	wrap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	wrap.add_child(n)
	return wrap

## 贴图加载。三种输入：
##   "res://…"     —— 导出路径。用 load() 拿到**有 resource_path 的资源**，这样 PackedScene
##                    保存的是 ext_resource 引用；若改用内存纹理，几 MB 的像素会被内联进 .tscn。
##   绝对路径      —— 预览路径。用 Image 读像素（用户的 .dswproj 不在 res:// 下）。
##   相对路径      —— 拼上 project_dir 后再按上一条处理。
static func _load_texture(rel: String, project_dir: String) -> Texture2D:
	if rel.is_empty():
		return null
	if rel.begins_with("res://"):
		if not ResourceLoader.exists(rel):
			return null
		return load(rel) as Texture2D
	var path := rel
	if not rel.contains("://"):
		if project_dir.is_empty():
			return null
		path = project_dir.path_join(rel)
	if not FileAccess.file_exists(path):
		return null
	var img := Image.new()
	if img.load(path) != OK:
		return null
	return ImageTexture.create_from_image(img)
```

- [ ] **Step 4: 实现 `scene_builder.gd`**

```gdscript
class_name SceneBuilder
## 文档 -> 完整 Node 树。**预览和导出共用这一条路径**（spec §2.3 的不变式）。
## 阶段 1 只做 color/image/text/particles 的静态形态；时间轴与效果栈分别在阶段 2/3 接入
## （两个接缝位置在 build() 里以注释标出）。

const ROOT_NAME := "WallpaperRoot"

## project_dir：用户的 .dswproj 目录（预览时是它，导出时是构建目录的父级）。
## 传空字符串表示"资源用 res:// 路径"。
static func build(doc: StudioDoc, project_dir: String) -> Control:
	var root := Control.new()
	root.name = ROOT_NAME
	root.size = Vector2(doc.canvas)
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var bg := ColorRect.new()
	bg.name = "background"
	bg.color = doc.background
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(bg)

	for i in doc.layers.size():
		var layer: Dictionary = doc.layers[i]
		var node := LayerNodes.build(layer, project_dir)
		if node == null:
			continue
		node.name = "layer_%d" % i
		_apply_common(node, layer)
		# 透明度必须挂在一个中间 Control 上，否则会盖掉节点自己的 modulate 语义
		# （比如 ColorRect.color 和 modulate 是相乘的，直接改 modulate 也能用，
		#  但粒子是 Node2D 走的是另一条路 —— 统一用包裹节点最省心）。
		root.add_child(node)

	# 阶段 2 的接缝：这里会调用 AnimationBuilder.compile(doc, root) 生成 AnimationPlayer。
	# 阶段 3 的接缝：LayerNodes.build 会在 layer.effects 非空时返回 SubViewport 链。
	return root

static func _apply_common(node: Control, layer: Dictionary) -> void:
	var t: Dictionary = layer.get("transform", {})
	node.position = Vector2(float(t.get("x", 0.0)), float(t.get("y", 0.0)))
	node.pivot_offset = Vector2.ZERO
	var s := float(t.get("scale", 1.0))
	node.scale = Vector2(s, s)
	node.rotation_degrees = float(t.get("rotation", 0.0))
	node.modulate = Color(1, 1, 1, float(t.get("opacity", 1.0)))
	node.visible = bool(layer.get("visible", true))
	node.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_apply_blend(node, String(layer.get("blend", "mix")))

static func _apply_blend(node: Control, blend: String) -> void:
	# mix 不需要材质。其余用 CanvasItemMaterial 的 blend_mode。
	if blend == "mix":
		return
	var mat := CanvasItemMaterial.new()
	match blend:
		"add":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
		"sub":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_SUB
		"mul":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_MUL
		_:
			return  # screen / overlay 阶段 3 用自定义着色器实现
	node.material = mat
```

- [ ] **Step 5: 跑，确认通过**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: `tests: 17/17 cases run, 0 failures`。

- [ ] **Step 6: 提交**

```bash
git add tools/wallpaper-studio/project/src/build tools/wallpaper-studio/project/tests/test_scene_builder.gd
git commit -m "feat(studio): shared document-to-scene builder for preview and export"
```

---

### Task 10: 效果库数据（阶段 3 才用，这里先定真源）

**Files:**
- Create: `tools/wallpaper-studio/project/src/effects/builtin_effects.gd`
- Create: `tools/wallpaper-studio/project/tests/test_effects.gd`

- [ ] **Step 1: 写失败测试**

`tests/test_effects.gd`：

```gdscript
extends RefCounted

static func _register() -> void:
	DTest.case("effects_have_unique_ids", func() -> void:
		var seen := {}
		for e in BuiltinEffects.ALL:
			DTest.ok(not seen.has(e.id), "duplicate effect id: " + e.id)
			seen[e.id] = true
		DTest.eq(seen.size(), 7, "expected 7 built-in effects")
	)
	DTest.case("effects_every_shader_compiles", func() -> void:
		for e in BuiltinEffects.ALL:
			var sh := Shader.new()
			sh.code = e.code
			DTest.ok(sh.get_shader_uniform_list() != null, "shader parses: " + e.id)
	)
	DTest.case("effects_params_have_defaults", func() -> void:
		for e in BuiltinEffects.ALL:
			for p in e.params:
				DTest.ok(p.has("name") and p.has("default"), "param complete in " + e.id)
	)
	DTest.case("effects_lookup_by_id", func() -> void:
		DTest.ok(BuiltinEffects.find("water") != null, "water exists")
		DTest.eq(BuiltinEffects.find("nope"), null, "unknown returns null")
	)

_register()
```

- [ ] **Step 2: 跑，确认失败**

Expected: `BuiltinEffects` 未声明。

- [ ] **Step 3: 实现**

创建 `src/effects/builtin_effects.gd`。**把 `tools/godot-src/editor/plugins/wallpaper_effects_builtin.h`
里 6 个效果 + `tools/godot-wallpaper-template/paint/wallpaper_effects.gdshader` 的遮罩效果原样搬过来**，
每个条目是：`{id, name, category, params: [{name, label, default, min, max}], code}`。
这是效果着色器的**唯一真源** —— `wallpaper_effects_builtin.h` 此后不再新增效果。

条目清单（id / 名称 / 分类 / 参数）见 spec §4.3 的表。`find(id)` 返回条目或 `null`。

- [ ] **Step 4: 跑，确认通过**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: `tests: 21/21 cases run, 0 failures`。

- [ ] **Step 5: 提交**

```bash
git add tools/wallpaper-studio/project/src/effects tools/wallpaper-studio/project/tests/test_effects.gd
git commit -m "feat(studio): built-in effect library as the single source of truth"
```

---

### Task 11: 导出器

**Files:**
- Create: `tools/wallpaper-studio/project/src/export/manifest.gd`
- Create: `tools/wallpaper-studio/project/src/export/packer.gd`
- Create: `tools/wallpaper-studio/project/src/export/poster.gd`
- Create: `tools/wallpaper-studio/project/src/export/exporter.gd`
- Create: `tools/wallpaper-studio/project/tests/test_exporter.gd`

- [ ] **Step 1: 写失败测试**

`tests/test_exporter.gd`：

```gdscript
extends RefCounted
## 注意：这个用例会在进程内挂载导出的 PCK。引擎是进程级单例、挂载不可卸载，
## 所以它必须在独立 runner 进程里跑：
##   run_tests.ps1 -Only exporter_produces_loadable_pack

static func _tmp_project() -> String:
	var dir := "user://test_proj"
	DirAccess.make_dir_recursive_absolute(dir)
	# 一张 4x4 的纯色 PNG 当图层资源
	var img := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 0, 0))
	img.save_png(dir.path_join("red.png"))
	return dir

static func _tmp_poster() -> String:
	var p := "user://test_poster.png"
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 1))
	img.save_png(p)
	return p

## 从 .dswall 里取出 project.pck 写成文件。只为测试存在。
static func _pck_from_dswall(dswall: String, out_pck: String) -> bool:
	var zip := ZIPReader.new()
	if zip.open(dswall) != OK:
		return false
	var bytes := zip.read_file("project.pck")
	zip.close()
	if bytes.is_empty():
		return false
	var f := FileAccess.open(out_pck, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true

static func _register() -> void:
	DTest.case("exporter_reports_validation_problems", func() -> void:
		var doc := StudioDoc.create_empty("坏壁纸", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_image("花瓣", "missing.png"))
		var problems := doc.validate("user://nowhere")
		DTest.eq(problems.size(), 1, "one problem")
		DTest.ok(String(problems[0]).contains("missing.png"), "names the missing file")
		DTest.eq(Exporter.export(doc, "user://nowhere", "user://out.dswall", "").ok, false, "export blocked")
	)
	DTest.case("exporter_writes_ext_resource_not_inline_pixels", func() -> void:
		# 关键不变式：贴图必须是 ext_resource 引用，不能把像素内联进 .tscn。
		# 内联会让一张 4K 贴图撑出几十 MB 的场景文件。
		var proj := _tmp_project()
		var doc := StudioDoc.create_empty("引用检查", Vector2i(320, 240))
		doc.add_layer(Layer.make_image("红块", "red.png"))
		var result: Dictionary = Exporter.export(doc, proj, "user://ref.dswall", "")
		DTest.eq(result.ok, true, "export ok: " + String(result.error))
		var tscn := FileAccess.get_file_as_string(ProjectSettings.globalize_path(
				StudioPaths.build_dir(doc.pack_id).path_join("main.tscn")))
		DTest.ok(tscn.contains("[ext_resource type=\"Texture2D\""), "texture is an ext_resource")
		DTest.ok(tscn.contains("res://build/" + doc.pack_id + "/assets/red.png"),
				"ext_resource points into the pack")
	)
	DTest.case("exporter_produces_loadable_pack", func() -> void:
		var proj := _tmp_project()
		var doc := StudioDoc.create_empty("导出台", Vector2i(320, 240))
		doc.add_layer(Layer.make_color("底", Color(0, 0, 0)))
		doc.add_layer(Layer.make_image("红块", "red.png"))
		var t := Layer.make_text("时钟", "00:00:00")
		t["dynamic"] = "clock"
		doc.add_layer(t)
		var out := "user://test_out.dswall"
		var result: Dictionary = Exporter.export(doc, proj, out, _tmp_poster())
		DTest.eq(result.ok, true, "export ok: " + String(result.error))

		# 产物结构
		var zip := ZIPReader.new()
		DTest.eq(zip.open(out), OK, "dswall opens as zip")
		var entries := zip.get_files()
		DTest.ok("project.pck" in entries, "has project.pck")
		DTest.ok("manifest.json" in entries, "has manifest.json")
		DTest.ok("poster.png" in entries, "has poster.png")

		var m: Dictionary = JSON.parse_string(zip.read_file("manifest.json").get_string_from_utf8())
		DTest.eq(m.dimension, "2d", "dimension is 2d")
		DTest.eq(int(m.format_version), 1, "format version")
		DTest.eq(String(m.main_scene).begins_with("res://build/"), true, "main scene under build/")
		DTest.eq(m.content_flags, [], "no scripts flagged (the clock script is ours)")
		zip.close()

		# 加密包能被本进程挂载。引擎是进程级单例、挂载不可卸载，
		# 所以本用例必须独占一个 runner 进程（--only=exporter_produces_loadable_pack）。
		var pck := "user://test_out.pck"
		DTest.eq(_pck_from_dswall(out, pck), true, "extracted pck")
		DTest.eq(ProjectSettings.load_resource_pack(ProjectSettings.globalize_path(pck), false), true, "pack mounts")
		DTest.ok(ResourceLoader.exists(String(m.main_scene)), "main scene loadable from pack")

		# 时钟脚本在包里仍然活着（预览与导出共用同一份源码，这是它的最终验证）
		var scene := load(String(m.main_scene)) as PackedScene
		var root := scene.instantiate()
		var found_script := false
		for c in root.get_children():
			if c.get_script() != null:
				found_script = true
				DTest.eq(c.get("mode"), "clock", "clock mode survived packing")
		root.free()
		DTest.eq(found_script, true, "clock script survived the pack")
	)

_register()
```

> 注意：上面注释里说的“独立 runner 进程”是必须的 —— `ProjectSettings.load_resource_pack`
> 一旦挂载就不能卸载，而引擎是进程级单例。`run_tests.ps1` 需要支持 `-Only <用例名>`
> （Task 6 Step 2 的 `--only=` 已经支持），并且在跑全量时**跳过**这个用例。
> 具体做法：`run_tests.gd` 把用例分成“可同进程”与“必须独占”两组，
> `run_tests.ps1` 先跑前者，再用 `-Only exporter_produces_loadable_pack` 单跑后者，
> 两次退出码都为 0 才算通过。

- [ ] **Step 2: 跑，确认失败**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
```

Expected: `Exporter` 未声明。

- [ ] **Step 3: 实现 `permissions.gd`**

```gdscript
class_name Permissions
## 从文档**保守地**推导 requested_permissions（spec §7.1）。
## 推导不出来的一律不声明 —— 宁可少报，也不虚报。

const NETWORK_MARKERS := ["HTTPRequest", "HTTPClient", "WebSocketPeer"]
const FILE_MARKERS := ["FileAccess", "DirAccess"]
const EXEC_MARKERS := ["OS.execute", "OS.create_process", "OS.shell_open"]
const SYSTEM_MARKERS := ["DisplayServer", "Input.parse_input_event", "OS.set_environment"]

static func derive(doc: StudioDoc) -> Array:
	var source := _external_script_sources(doc)
	var out := []
	if _any(source, NETWORK_MARKERS):
		out.append("network")
	if _any(source, FILE_MARKERS):
		out.append("file_outside")
	if _any(source, EXEC_MARKERS):
		out.append("exec")
	if _any(source, SYSTEM_MARKERS):
		out.append("system")
	return out

static func _external_script_sources(doc: StudioDoc) -> String:
	var all := ""
	for rel in doc.external_scripts():
		# 预览时脚本在用户工程目录；这里只拿得到相对路径，所以从 res:// 试一次、
		# 再由调用方（导出时已知绝对路径）补上。取不到就当空文本 —— 不声明任何权限。
		if FileAccess.file_exists(rel):
			all += FileAccess.get_file_as_string(rel)
	return all

static func _any(haystack: String, needles: Array) -> bool:
	for n in needles:
		if haystack.contains(String(n)):
			return true
	return false
```

- [ ] **Step 4: 实现 `manifest.gd`**

```gdscript
class_name StudioManifest
## manifest.json 组装。字段与 WallPaperPackage.h 的 WallPaperManifest 一一对应。

static func build(doc: StudioDoc, main_scene: String, content_flags: Array) -> Dictionary:
	var vi := Engine.get_version_info()
	return {
		"format_version": 1,
		"title": doc.name,
		"author": "",
		"description": "",
		"pack_id": doc.pack_id,
		"dimension": "2d",
		"tags": [],
		"categories": [],
		"main_scene": main_scene,
		"engine": {
			"version": "%d.%d.%d" % [vi.major, vi.minor, vi.patch],
			"build": vi.string,
		},
		"created_utc": Time.get_datetime_string_from_system(true, true),
		"requested_permissions": Permissions.derive(doc),
		"content_flags": content_flags,
	}
```

- [ ] **Step 4: 实现 `packer.gd`**

```gdscript
class_name Packer
## 加密 PCK + .dswall 容器。
##
## 密钥来自 <studio>/signing_key.txt，与烧进 libgodot 的 script_encryption_key 是同一个值
## （见 tools/build_godot.ps1）。引擎读包时用编译进去的密钥，宿主不需要传。

static func load_key() -> String:
	var path := "res://signing_key.txt"
	if not FileAccess.file_exists(path):
		return ""
	var key := FileAccess.get_file_as_string(path).strip_edges()
	if key.length() != 64 or not key.is_valid_hex_number(false) or key == "0".repeat(64):
		return ""
	return key

## 把 disk_dir 下所有文件按 res://<prefix>/<rel> 打成一个加密 PCK。
static func pack_encrypted(disk_dir: String, res_prefix: String, out_pck: String, key_hex: String) -> Error:
	var files: PackedStringArray = _walk(disk_dir)
	if files.is_empty():
		return ERR_FILE_NOT_FOUND
	var packer := PCKPacker.new()
	var err := packer.pck_start(out_pck, 32, key_hex, true)
	if err != OK:
		return err
	for f in files:
		var rel: String = f.substr(disk_dir.length()).trim_prefix("/").replace("\\", "/")
		var target := "res://" + res_prefix.path_join(rel).replace("//", "/")
		err = packer.add_file(target, f, true)
		if err != OK:
			return err
	return packer.flush()

## 把三个文件装成 .dswall（ZIP）。先写 .tmp 再改名，保证不会产生半个包。
## poster_path 为空或不存在时，poster.png 条目会被跳过（阶段 1 允许没有海报）。
static func assemble_dswall(out_path: String, pck_path: String, manifest_text: String, poster_path: String) -> Error:
	var tmp := out_path + ".tmp"
	if FileAccess.file_exists(tmp):
		DirAccess.remove_absolute(tmp)
	var zip := ZIPPacker.new()
	if zip.open(tmp) != OK:
		return ERR_CANT_CREATE
	var ok := true
	ok = _add(zip, "project.pck", pck_path) and ok
	ok = _add_text(zip, "manifest.json", manifest_text) and ok
	if not poster_path.is_empty() and FileAccess.file_exists(poster_path):
		ok = _add(zip, "poster.png", poster_path) and ok
	zip.close()
	if not ok:
		DirAccess.remove_absolute(tmp)
		return ERR_CANT_CREATE
	if FileAccess.file_exists(out_path):
		DirAccess.remove_absolute(out_path)
	return DirAccess.rename_absolute(tmp, out_path)

static func _add(zip: ZIPPacker, name: String, disk_path: String) -> bool:
	if not FileAccess.file_exists(disk_path):
		return false
	var bytes := FileAccess.get_file_as_bytes(disk_path)
	if bytes.is_empty():
		return false
	zip.start_file(name)
	return zip.write_file(bytes) == OK

static func _add_text(zip: ZIPPacker, name: String, text: String) -> bool:
	zip.start_file(name)
	return zip.write_file(text.to_utf8_buffer()) == OK

static func _walk(dir_path: String) -> PackedStringArray:
	var out := PackedStringArray()
	var stack := [dir_path]
	while not stack.is_empty():
		var d: String = stack.pop_back()
		var da := DirAccess.open(d)
		if da == null:
			continue
		da.list_dir_begin()
		var n := da.get_next()
		while n != "":
			if n != "." and n != "..":
				var full := d.path_join(n)
				if da.current_is_dir():
					stack.push_back(full)
				else:
					out.append(full.replace("\\", "/"))
			n = da.get_next()
	return out
```

- [ ] **Step 5: 实现 `poster.gd`**

```gdscript
class_name Poster
## 离屏渲染主场景一帧，出海报。共用 SceneBuilder，所以海报与实机渲染一致。

const SIZE := Vector2i(960, 540)

static func render(doc: StudioDoc, project_dir: String, out_png: String, tree: SceneTree) -> bool:
	var sub := SubViewport.new()
	sub.size = SIZE
	sub.transparent_bg = false
	sub.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	sub.own_world_3d = true
	tree.root.add_child(sub)

	var root := SceneBuilder.build(doc, project_dir)
	# 海报是 960x540 的固定尺寸，把文档画布缩放进取景框，取整幅内容。
	var scale := minf(float(SIZE.x) / float(doc.canvas.x), float(SIZE.y) / float(doc.canvas.y))
	root.size = Vector2(doc.canvas) * scale
	root.position = (Vector2(SIZE) - root.size) * 0.5
	sub.add_child(root)

	# 两帧：一帧让节点进入树并完成布局，一帧让渲染目标真正画出来。
	await tree.process_frame
	await tree.process_frame
	await RenderingServer.frame_post_draw
	var img := sub.get_texture().get_image()
	sub.queue_free()
	if img == null:
		return false
	return img.save_png(out_png) == OK
```

- [ ] **Step 6: 实现 `exporter.gd`**

```gdscript
class_name Exporter
## 导出编排。**完全同步**，不弹 UI，不在内部 await —— 海报由调用方（UI）先渲染好再传进来。
## 这样整个导出链路都能被无头测试直接调用。

static func export(doc: StudioDoc, project_dir: String, out_path: String, poster_path: String) -> Dictionary:
	# 1) 校验（阻塞式：有问题就一个文件都不生成）
	var problems := doc.validate(project_dir)
	if not problems.is_empty():
		return { "ok": false, "error": "校验未通过", "problem_list": problems, "path": "" }

	var key := Packer.load_key()
	if key.is_empty():
		return { "ok": false, "error": "缺少或非法的 signing_key.txt（studio 目录下）", "problem_list": [], "path": "" }

	# 2) 构建目录：只放 res:// 内容（main.tscn + assets），整目录重建。
	#    它必须在 res:// 下，ResourceSaver 才会把路径本地化成 res:// 相对路径（spec §2.2）。
	var build_res := StudioPaths.build_dir(doc.pack_id)
	var build_abs := ProjectSettings.globalize_path(build_res)
	_delete_dir_recursive(build_abs)
	DirAccess.make_dir_recursive_absolute(build_abs)
	var assets_res := build_res.path_join("assets")
	var assets_abs := ProjectSettings.globalize_path(assets_res)
	DirAccess.make_dir_recursive_absolute(assets_abs)

	# 中间产物（海报、PCK）放 scratch，别混进被扫描的构建目录，否则会被打进包里。
	var scratch_abs := ProjectSettings.globalize_path(StudioPaths.scratch_dir(doc.pack_id))
	DirAccess.make_dir_recursive_absolute(scratch_abs)

	# 3) 拷资源。构建目录里按文件名平铺 —— 用户工程里 src 是相对路径，
	#    平铺后不会出现 assets/assets/a.png 这种嵌套。
	for l in doc.layers:
		var type := String(l.get("type", ""))
		var rel := ""
		if type == "image":
			rel = String(l.get("src", ""))
		elif type == "particles":
			rel = String(l.get("texture", ""))
		if rel.is_empty():
			continue
		if not _copy_file(project_dir.path_join(rel), assets_abs.path_join(rel.get_file())):
			return { "ok": false, "error": "复制资源失败：" + rel, "problem_list": [], "path": "" }

	# 4) 生成场景并保存。贴图以 res://build/<id>/assets/<name> 加载，
	#    PackedScene 因而保存成 ext_resource 引用而不是内联像素（见 test_exporter 的用例）。
	var root := SceneBuilder.build(doc, assets_res)
	var scene := PackedScene.new()
	if scene.pack(root) != OK:
		root.free()
		return { "ok": false, "error": "PackedScene.pack 失败", "problem_list": [], "path": "" }
	root.free()
	var main_scene_res := build_res.path_join("main.tscn")
	var save_err := ResourceSaver.save(scene, main_scene_res)
	if save_err != OK:
		return { "ok": false, "error": "保存场景失败（err=%d）。res:// 是否可写？Studio 必须以目录形式运行。" % save_err,
				"problem_list": [], "path": "" }

	# 5) 加密 PCK。用 res_prefix 让包内路径与 main.tscn 里的引用一致。
	var tmp_pck := scratch_abs.path_join("project.pck")
	var pack_err := Packer.pack_encrypted(build_abs, build_res.trim_prefix("res://"), tmp_pck, key)
	if pack_err != OK:
		return { "ok": false, "error": "打包 PCK 失败（err=%d）" % pack_err, "problem_list": [], "path": "" }

	# 6) manifest + .dswall
	var content_flags := []
	if not doc.external_scripts().is_empty():
		content_flags.append("scripts")
	var manifest_text := JSON.stringify(StudioManifest.build(doc, main_scene_res, content_flags), "  ")
	var zip_err := Packer.assemble_dswall(out_path, tmp_pck, manifest_text, poster_path)
	if zip_err != OK:
		return { "ok": false, "error": "写入 .dswall 失败（err=%d）" % zip_err, "problem_list": [], "path": "" }

	return { "ok": true, "error": "", "problem_list": [], "path": out_path }

## 投递到桌面贴纸收件箱（热应用）。先写 .tmp 再改名。
static func deliver_to_inbox(dswall_path: String) -> Dictionary:
	var inbox := StudioPaths.inbox_dir()
	if inbox.is_empty():
		return { "ok": false, "error": "读不到 %APPDATA%" }
	if DirAccess.make_dir_recursive_absolute(inbox) != OK:
		return { "ok": false, "error": "收件箱不可写：" + inbox }
	var final := inbox.path_join(dswall_path.get_file())
	var tmp := final + ".tmp"
	var bytes := FileAccess.get_file_as_bytes(dswall_path)
	if bytes.is_empty():
		return { "ok": false, "error": "读不到导出文件：" + dswall_path }
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return { "ok": false, "error": "收件箱不可写：" + tmp }
	f.store_buffer(bytes)
	f.close()
	if FileAccess.file_exists(final):
		DirAccess.remove_absolute(final)
	if DirAccess.rename_absolute(tmp, final) != OK:
		return { "ok": false, "error": "投递失败：" + final }
	return { "ok": true, "error": "", "path": final }

static func _copy_file(src: String, dst: String) -> bool:
	var bytes := FileAccess.get_file_as_bytes(src)
	if bytes.is_empty():
		return false
	var f := FileAccess.open(dst, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true

static func _delete_dir_recursive(abs_path: String) -> void:
	if not DirAccess.dir_exists_absolute(abs_path):
		return
	var da := DirAccess.open(abs_path)
	if da == null:
		return
	da.list_dir_begin()
	var n := da.get_next()
	while n != "":
		if n != "." and n != "..":
			var full := abs_path.path_join(n)
			if da.current_is_dir():
				_delete_dir_recursive(full)
			else:
				DirAccess.remove_absolute(full)
		n = da.get_next()
	DirAccess.remove_absolute(abs_path)
```

> 时钟脚本**不落盘**：`LayerNodes` 把它作为内存里的 `GDScript` 挂到节点上，
> `PackedScene.pack` 会把它作为 `sub_resource` 内联进 `.tscn`。
> 这样"预览与导出共用同一份源码"这条不变式更强（不存在两份文件可能不同步的问题），
> 也不需要把 Studio 自己生成的脚本算进 `content_flags`。
> `test_exporter.gd` 的第三个用例会实例化包里的场景并断言脚本仍然活着。

- [ ] **Step 7: 跑测试**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1 -Only exporter_produces_loadable_pack
```

Expected: 全量跑 `tests: 23/24 cases run, 0 failures`（独占用例被跳过），
单跑 `tests: 1/24 cases run, 0 failures`。两条命令退出码都是 0。

- [ ] **Step 8: 提交**

```bash
git add tools/wallpaper-studio/project/src/export tools/wallpaper-studio/project/tests/test_exporter.gd
git commit -m "feat(studio): encrypted dswall exporter with manifest and poster"
```


### Task 12: 界面（图层面板 / 画布 / 属性）

**Files:**
- Create: `tools/wallpaper-studio/project/src/ui/studio_main.gd`
- Create: `tools/wallpaper-studio/project/src/ui/layer_panel.gd`
- Create: `tools/wallpaper-studio/project/src/ui/canvas_view.gd`
- Create: `tools/wallpaper-studio/project/src/ui/inspector.gd`
- Create: `tools/wallpaper-studio/project/src/ui/project_bar.gd`
- Modify: `tools/wallpaper-studio/project/main.tscn`

- [ ] **Step 1: 搭布局**

`main.tscn` 改成三栏：顶部 36px 工具条（`ProjectBar`），中间 `HSplitContainer`
（左 240px `LayerPanel`，中 `CanvasView`，右 300px `Inspector`）。每个面板挂自己的脚本。

- [ ] **Step 2: `studio_main.gd`**

持有 `StudioDoc` 与当前工程目录，是各面板的唯一数据入口：

```gdscript
extends Control
## 界面总装。唯一持有 StudioDoc 的地方；面板只通过它读写文档并发信号。

signal doc_changed
signal selection_changed(index: int)

@onready var _bar: Control = $VBox/ProjectBar
@onready var _layers: Control = $VBox/HSplit/LayerPanel
@onready var _canvas: Control = $VBox/HSplit/CanvasView
@onready var _inspector: Control = $VBox/HSplit/Inspector

var doc: StudioDoc
var project_dir: String = ""
var selected: int = -1

func _ready() -> void:
	doc = StudioDoc.create_empty("未命名壁纸", Vector2i(1920, 1080))
	project_dir = ProjectSettings.globalize_path(StudioPaths.default_project_root()).path_join("未命名壁纸")
	DirAccess.make_dir_recursive_absolute(project_dir)
	_bar.setup(self)
	_layers.setup(self)
	_canvas.setup(self)
	_inspector.setup(self)
	refresh_all()

func refresh_all() -> void:
	doc_changed.emit()
	_layers.refresh()
	_canvas.rebuild()
	_inspector.refresh()

func select_layer(index: int) -> void:
	selected = index
	selection_changed.emit(index)
	_inspector.refresh()
```

- [ ] **Step 3: `layer_panel.gd`**

`ItemList` + 「＋ 添加图层」下拉（纯色/图片/文字/粒子）+ 「删除 / 上移 / 下移 / 显示切换」。
列表按文档倒序显示（顶部 = 最上层），下标换算在一处完成：

```gdscript
func _list_index_to_doc(row: int) -> int:
	# 列表顶部 = 最上层 = 文档最后一个元素
	return main.doc.layers.size() - 1 - row
```

- [ ] **Step 4: `canvas_view.gd`**

`SubViewportContainer` + `SubViewport`（尺寸 = 文档画布），里面每次 `rebuild()` 时
清空并用 `SceneBuilder.build(main.doc, main.project_dir)` 重建。容器按可用空间等比缩放：

```gdscript
func rebuild() -> void:
	for c in _viewport.get_children():
		c.queue_free()
	_viewport.size = main.doc.canvas
	var root := SceneBuilder.build(main.doc, main.project_dir)
	_viewport.add_child(root)
	_fit_to_available()

func _fit_to_available() -> void:
	var avail := size
	var s := minf(avail.x / float(main.doc.canvas.x), avail.y / float(main.doc.canvas.y))
	s = minf(s, 1.0)
	_container.scale = Vector2(s, s)
	_container.size = Vector2(main.doc.canvas) * s
```

- [ ] **Step 5: `inspector.gd`**

按选中图层的 `type` 动态建设控件（`name` / `visible` / `transform.x/y/scale/rotation/opacity` /
`blend` 是通用项；其余按类型）。任何改动 → 写回 `main.doc.layers[i]` → `main.refresh_all()`。

- [ ] **Step 6: `project_bar.gd`**

按钮：**新建 / 打开 / 保存 / 添加资源 / 导出 .dswall / 导出并应用到桌面**。
「新建」弹出一个只有标题 + 分辨率选项的对话框（1920×1080 / 2560×1440 / 3840×2160 / 自定义）。
「打开/保存」用 `FileDialog` 选 `.dswproj` 目录。
「导出」先 `await Poster.render(...)` 渲染海报到 scratch，再调**同步的**
`Exporter.export(doc, project_dir, out_path, poster_path)`；失败时把 `problem_list`
显示在一个 `AcceptDialog` 里。「导出并应用到桌面」在成功后接着调
`Exporter.deliver_to_inbox(path)`。

- [ ] **Step 7: 手动跑通**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File build.bat
"D:/project/Desktop Sticker/Desktop Sticker/x64/x64/Release/Desktop Sticker/dstk-studio.exe"
```

Expected: 三栏界面出现；添加一个纯色 + 一个文字（时间）图层，画布立刻显示钟表在走；
保存后在 `%APPDATA%\Godot\app_userdata\桌面贴纸 · 壁纸编辑器\projects\` 下出现 `.dswproj`。

- [ ] **Step 8: 提交**

```bash
git add tools/wallpaper-studio/project/src/ui tools/wallpaper-studio/project/main.tscn
git commit -m "feat(studio): three-pane editor ui with live preview"
```

---

### Task 13: 接入构建与测试

**Files:**
- Modify: `tools/rebuild_and_test.ps1`
- Modify: `AGENTS.md`

- [ ] **Step 1: 加 Studio 测试步骤**

在 `tools/rebuild_and_test.ps1` 跑完 dtest 之后、退出之前插入：

```powershell
if (-not $NoStudio) {
    $studioTest = Join-Path $PSScriptRoot 'wallpaper-studio\run_tests.ps1'
    if (Test-Path $studioTest) {
        Write-Host "[studio] running headless editor tests"
        & powershell -NoProfile -ExecutionPolicy Bypass -File $studioTest
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[studio] editor tests FAILED" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
}
```

并给参数块加 `[switch]$NoStudio`。

- [ ] **Step 2: 跑一遍完整链路**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev_build.ps1
```

Expected: dtest `232 passed, 0 failed`，随后 `[studio] … tests: 23/24 cases run, 0 failures`
与 `tests: 1/24 cases run, 0 failures`。

- [ ] **Step 3: 更新 AGENTS.md**

在 Layout 段加 `Desktop Sticker/DesktopSticker.WallpaperStudio/` 与 `tools/wallpaper-studio/`；
在 Build & test 段加 Studio 的构建/运行/无头测试命令；在 Fragile areas 段加两条：
① Studio 必须以**目录**形式分发，因为导出需要可写的 `res://`；
② 密钥烧进 DLL，加密只防随手翻看不构成安全边界。

- [ ] **Step 4: 端到端人工验收**

1. 用 Studio 做一个「纯色背景 + 时钟文字 + 一张图」的壁纸并「导出并应用到桌面」；
2. 打开桌面贴纸 → 设置 → 壁纸，确认新壁纸已在列表并已应用；
3. `tools/screen_capture.ps1` 截图确认桌面渲染正确；
4. 按 `docs/acceptance.md` 走一遍切换与退出（切换回视频壁纸 → 退出恢复原生桌面图标）。

- [ ] **Step 5: 提交**

```bash
git add tools/rebuild_and_test.ps1 AGENTS.md
git commit -m "build(studio): wire the editor into the build and test loop"
```

---

## 阶段 2–4 的接缝

这三个阶段各自单独成计划，从同一份 spec 派生。它们**必须**遵守的约束（写在这里，
避免下一份计划猜错）：

1. **阶段 2（时间轴）**：`scene_builder.gd` 里已经留了 `AnimationBuilder.compile(doc, root)`
   的调用点（Task 9 Step 4 的注释）。时间轴求值的纯函数放 `src/doc/timeline.gd`，
   编译成 `AnimationPlayer` 的代码放 `src/build/animation_builder.gd`。**不要在预览里
   用 `_process` 手算关键帧** —— 预览必须走与导出同一份编译结果，否则违反 §2.3 的不变式。
2. **阶段 3（效果栈/粒子/自定义着色器）**：`LayerNodes.build` 是唯一改动点 ——
   `layer.effects` 非空时返回 `SubViewport` 链（spec §5.3），链的第 0 趟吃原始节点。
   `BuiltinEffects` 已经是真源，不要再往 `wallpaper_effects_builtin.h` 加效果。
   `LayerNodes.DSTK_CLOCK_SCRIPT` 是"Studio 生成的唯一脚本"这条规则的一部分，
   新增生成脚本时必须同步更新 `Exporter._needs_clock_script` 与 `content_flags` 的判定。
3. **阶段 4（鼠标视差）**：要在 fork 里加一个 `dstk_godot_set_cursor(int x, int y)` 导出
   （`platform/windows/libgodot_windows.cpp`），由壁纸宿主每帧喂光标位置，
   再在 Godot 侧用一个 `Engine.register_singleton`/自动加载单例暴露给视差节点。
   这条改的是**桌面贴纸的壁纸宿主**（`WallPaperWindow` / `GodotEmbeddedBackend`），
   与 Studio 无关；验收必须包含"切换回视频壁纸后视差残留位置不粘住"。
