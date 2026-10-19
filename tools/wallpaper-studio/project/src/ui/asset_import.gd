extends RefCounted
## 把用户从任意位置选的图片拷进工程的 assets/，返回相对工程目录的路径。
##
## 为什么必须拷贝：文档里的图片路径是**相对工程目录**的（导出时按相对路径取文件），
## 直接引用 C:\Users\...\图片.png 会让工程在换机器后立刻失效。
##
## 去重规则：目标文件已存在且字节相同 -> 直接复用；同名但内容不同 -> 追加 _1/_2 后缀。
## 重复选同一张图是常见操作（换图层时误点一次），每次都产生副本会让 assets/ 迅速变脏。

const StudioPaths = preload("res://src/doc/paths.gd")
const MAX_SUFFIX := 999
const IMAGE_EXTS := ["png", "jpg", "jpeg", "webp", "bmp", "tga"]


## 返回 "assets/xxx.png"，失败返回 ""。
static func import_image(project_dir: String, abs_path: String) -> String:
	if project_dir.is_empty() or abs_path.is_empty():
		return ""
	var bytes := FileAccess.get_file_as_bytes(abs_path)
	if bytes.is_empty():
		return ""
	var assets := project_dir.path_join("assets")
	if DirAccess.make_dir_recursive_absolute(assets) != OK:
		return ""

	var file_name := abs_path.get_file()
	if file_name.is_empty():
		return ""
	var stem := file_name.get_basename()
	var ext := file_name.get_extension()
	var suffix := "" if ext.is_empty() else "." + ext

	var n := 1
	while n <= MAX_SUFFIX:
		var candidate := file_name if n == 1 else "%s_%d%s" % [stem, n - 1, suffix]
		var target := assets.path_join(candidate)
		if not FileAccess.file_exists(target):
			if not _write_bytes(target, bytes):
				return ""
			return "assets/" + candidate
		if FileAccess.get_file_as_bytes(target) == bytes:
			return "assets/" + candidate
		n += 1
	return ""


## 选择图片时用的过滤器串（FileDialog.filters 用的格式）。
## include_gif 只给"图片图层的 src"用：那里会把 GIF 拆成帧序列。
## 粒子贴图 / 遮罩图不认识 GIF，列出来只会让人选中后得到一个看不见的图层。
static func image_filters(include_gif: bool = false) -> PackedStringArray:
	if include_gif:
		return PackedStringArray(["*.png, *.jpg, *.jpeg, *.webp, *.bmp, *.tga, *.gif ; 图片 / GIF 动图"])
	return PackedStringArray(["*.png, *.jpg, *.jpeg, *.webp, *.bmp, *.tga ; 图片"])


# ---------------------------------------------------------------- 动图（帧序列）

## 把一个目录里的图片拷进 assets/<名字>_frames/，返回按文件名排好序的相对路径。
## 单独建目录（而不是平铺进 assets/）是因为导出的 `.dswall` 里帧要能一眼看出属于谁，
## 而且同名帧（frame_0001.png）在不同图层之间不会互相覆盖。
static func import_frames(project_dir: String, abs_dir: String, name_hint: String) -> Array:
	var out := []
	if project_dir.is_empty() or abs_dir.is_empty():
		return out
	var files := []
	var da := DirAccess.open(abs_dir)
	if da == null:
		return out
	for f in da.get_files():
		if String(f).get_extension().to_lower() in IMAGE_EXTS:
			files.append(String(f))
	if files.is_empty():
		return out
	files.sort()

	var stem := name_hint.strip_edges()
	if stem.is_empty():
		stem = abs_dir.get_file()
	if stem.is_empty():
		stem = "frames"
	var base := "%s_frames" % StudioPaths.safe_name(stem)
	var folder := base
	var n := 2
	while DirAccess.dir_exists_absolute(project_dir.path_join("assets").path_join(folder)):
		folder = "%s_%d" % [base, n]
		n += 1
	var target_abs := project_dir.path_join("assets").path_join(folder)
	if DirAccess.make_dir_recursive_absolute(target_abs) != OK:
		return out
	# 文件名带上前缀：导出时会按**文件名**把资源平铺进 assets/（见 exporter），
	# 不重命名的话两个 GIF 都叫 frame_0001.png 就会撞在一起、直接导出失败。
	var index := 1
	for f in files:
		var bytes := FileAccess.get_file_as_bytes(abs_dir.path_join(f))
		if bytes.is_empty():
			continue
		var ext := String(f).get_extension().to_lower()
		var out_name := "%s_%04d.%s" % [folder, index, ext]
		var dst := target_abs.path_join(out_name)
		var fh := FileAccess.open(dst, FileAccess.WRITE)
		if fh == null:
			continue
		fh.store_buffer(bytes)
		fh.close()
		out.append("assets/%s/%s" % [folder, out_name])
		index += 1
	return out


## GIF -> PNG 帧序列。用仓库自带的 ffmpeg（tools/ffmpeg），找不到就返回错误让用户手动拆帧。
## 返回 {ok, frames: Array, fps: float, error: String}
static func import_gif(project_dir: String, gif_path: String) -> Dictionary:
	var ff := ffmpeg_path()
	if ff.is_empty():
		return {"ok": false, "frames": [], "fps": 12.0,
				"error": "找不到 ffmpeg.exe（tools/ffmpeg/）。\n可以先用其它工具把 GIF 拆成 PNG 帧，"
						+ "再用「导入帧序列目录」。"}
	var tmp := ProjectSettings.globalize_path("user://tmp/gif_frames")
	_clear_dir(tmp)
	DirAccess.make_dir_recursive_absolute(tmp)

	var output := []
	var args := PackedStringArray(["-y", "-hide_banner", "-loglevel", "error", "-i", gif_path,
			"-vsync", "0", tmp.path_join("frame_%04d.png")])
	var code := OS.execute(ff, args, output, true)
	if code != 0:
		return {"ok": false, "frames": [], "fps": 12.0,
				"error": "ffmpeg 拆帧失败（退出码 %d）：\n%s" % [code, "\n".join(output)]}

	var frames := import_frames(project_dir, tmp,
			gif_path.get_file().get_basename())
	if frames.is_empty():
		return {"ok": false, "frames": [], "fps": 12.0, "error": "GIF 里没有解出任何帧。"}
	return {"ok": true, "frames": frames, "fps": probe_gif_fps(ff, gif_path), "error": ""}


## 用 ffmpeg 的 -i 输出（stderr）里的 "N fps" 反推帧率；拿不到就用 12。
static func probe_gif_fps(ff: String, gif_path: String) -> float:
	var out := []
	OS.execute(ff, PackedStringArray(["-hide_banner", "-i", gif_path]), out, true)
	var re := RegEx.new()
	re.compile("([0-9]+(?:\\.[0-9]+)?) fps")
	for line in out:
		var m := re.search(String(line))
		if m != null:
			return clampf(float(m.get_string(1)), 0.5, 60.0)
	return 12.0


## 找 ffmpeg.exe。Studio 有两种运行位置：<exeDir>/ffmpeg/（分发时拷过去）
## 和仓库里的 tools/ffmpeg/（从 x64/Release/Desktop Sticker 往上四层）。
static func ffmpeg_path() -> String:
	var exe_dir := OS.get_executable_path().get_base_dir()
	var candidates := [
		exe_dir.path_join("ffmpeg/ffmpeg.exe"),
		exe_dir.path_join("../ffmpeg/ffmpeg.exe"),
		exe_dir.path_join("../../../../tools/ffmpeg/ffmpeg.exe"),
		exe_dir.path_join("../../../tools/ffmpeg/ffmpeg.exe"),
	]
	for c in candidates:
		var p: String = String(c).simplify_path()
		if FileAccess.file_exists(p):
			return p
	return ""


static func _clear_dir(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	for f in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(f))
	for d in DirAccess.get_directories_at(path):
		_clear_dir(path.path_join(d))


static func _write_bytes(path: String, bytes: PackedByteArray) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true
