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
| `project/` | Studio 的 Godot 工程（阶段 1 起） |

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
