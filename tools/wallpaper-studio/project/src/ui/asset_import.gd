extends RefCounted
## 把用户从任意位置选的图片拷进工程的 assets/，返回相对工程目录的路径。
##
## 为什么必须拷贝：文档里的图片路径是**相对工程目录**的（导出时按相对路径取文件），
## 直接引用 C:\Users\...\图片.png 会让工程在换机器后立刻失效。
##
## 去重规则：目标文件已存在且字节相同 -> 直接复用；同名但内容不同 -> 追加 _1/_2 后缀。
## 重复选同一张图是常见操作（换图层时误点一次），每次都产生副本会让 assets/ 迅速变脏。

const MAX_SUFFIX := 999


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
static func image_filters() -> PackedStringArray:
	return PackedStringArray(["*.png, *.jpg, *.jpeg, *.webp, *.bmp, *.tga ; 图片"])


static func _write_bytes(path: String, bytes: PackedByteArray) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true
