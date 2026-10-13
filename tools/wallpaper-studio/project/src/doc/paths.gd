extends RefCounted
## Studio 用到的路径计算。全部是纯函数 —— 不读盘、不建目录，便于无头测试。
##
## 注意：本工程不用 class_name。全局类名依赖 .godot/global_script_class_cache.cfg，
## 那是编辑器生成的产物；Studio 以源码目录形式直接跑在 template_release 运行库上，
## 没有这个缓存，class_name 引用会以 "Identifier not declared" 直接失败。
## 所以每个文件都用 preload 常量显式引入依赖。

## 构建目录必须在 res:// 下：ResourceSaver 会把写在工程目录里的绝对路径自动本地化成
## res:// 相对路径，导出时 PCK 里的路径因此天然对上（设计 §2.2）。
## 也正因为如此，Studio 必须以**目录**形式分发 —— 打成 pck 时 res:// 是只读的。
static func build_dir(pack_id: String) -> String:
	return "res://build/" + pack_id

## 用户工程（.dswproj 目录）默认落在 user:// 下，跟安装目录分开。
static func default_project_root() -> String:
	return "user://projects"

## 导出的中间产物（海报、PCK）。刻意**不**放 build_dir 里，否则会被整目录打包进包里。
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
		var ch := title[i]
		if ch in ["\\", "/", ":", "*", "?", "\"", "<", ">", "|"]:
			out += "_"
		elif ch.unicode_at(0) < 32:
			out += "_"
		else:
			out += ch
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
