extends SceneTree
## 打包工具：把本工程打成 project.pck，再装成 .dswall（ZIP：project.pck + manifest.json + poster.png）。
## 用法（模板运行库或编辑器都行）：
##   godot.windows.template_release.x86_64.exe --headless --path samples/keyboard-test-src --script res://tools/pack.gd
## 产物写在本工程的 out/ 目录（已被 .gitignore 忽略），再拷回 samples/ 覆盖旧包。

const OUT_DIR := "res://out"
const PACK_NAME := "3d-keyboard-test.dswall"
const TITLE := "3D 键盘 / 鼠标测试台"
const FILES := ["project.godot", "main.tscn", "main.gd"]


func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute(OUT_DIR)

	var pck_path := OUT_DIR.path_join("project.pck")
	if FileAccess.file_exists(pck_path):
		DirAccess.remove_absolute(pck_path)

	var packer := PCKPacker.new()
	var err := packer.pck_start(pck_path, 32)
	if err != OK:
		push_error("pck_start 失败：%d" % err)
		quit(1)
		return
	for rel in FILES:
		var abs_path := ProjectSettings.globalize_path("res://" + rel)
		err = packer.add_file("res://" + rel, abs_path)
		if err != OK:
			push_error("add_file 失败 %s：%d" % [rel, err])
			quit(1)
			return
	err = packer.flush()
	if err != OK:
		push_error("flush 失败：%d" % err)
		quit(1)
		return

	var poster_path := OUT_DIR.path_join("poster.png")
	_make_poster(poster_path)

	var vi := Engine.get_version_info()
	var manifest := {
		"format_version": 1,
		"title": TITLE,
		"author": "Desktop Sticker",
		"description": "按键（单键 / 组合键）与鼠标（左 / 右 / 中键、滚轮、拖动）输入测试台",
		"main_scene": "res://main.tscn",
		"engine": {
			"version": "%d.%d.%d" % [vi.major, vi.minor, vi.patch],
			"build": vi.string,
		},
		"created_utc": Time.get_datetime_string_from_system(true, true),
		"requested_permissions": [],
		"content_flags": ["scripts"],
	}

	var dswall_path := OUT_DIR.path_join(PACK_NAME)
	if FileAccess.file_exists(dswall_path):
		DirAccess.remove_absolute(dswall_path)
	var zip := ZIPPacker.new()
	if zip.open(dswall_path) != OK:
		push_error("无法创建 .dswall")
		quit(1)
		return
	var ok := true
	ok = _add_bytes(zip, "project.pck", FileAccess.get_file_as_bytes(pck_path)) and ok
	ok = _add_bytes(zip, "manifest.json", JSON.stringify(manifest, "  ").to_utf8_buffer()) and ok
	ok = _add_bytes(zip, "poster.png", FileAccess.get_file_as_bytes(poster_path)) and ok
	zip.close()

	if not ok:
		push_error("写入 .dswall 条目失败")
		quit(1)
		return

	print("PACK OK: ", dswall_path, "  (", FileAccess.get_file_as_bytes(dswall_path).size(), " bytes)")
	quit(0)


func _add_bytes(zip: ZIPPacker, name: String, bytes: PackedByteArray) -> bool:
	zip.start_file(name)
	return zip.write_file(bytes) == OK


func _make_poster(path: String) -> void:
	var img := Image.create(1280, 720, false, Image.FORMAT_RGB8)
	var top := Color(0.05, 0.07, 0.11)
	var bottom := Color(0.02, 0.03, 0.05)
	for y in 720:
		var t := float(y) / 719.0
		img.fill_rect(Rect2i(0, y, 1280, 1), top.lerp(bottom, t))
	# 键盘示意：四行键帽 + 空格键
	for r in 4:
		for c in 10:
			var rect := Rect2i(200 + c * 92, 300 + r * 74, 78, 62)
			var col := Color(0.12, 0.15, 0.21)
			if r == 1 and c == 2:
				col = Color(0.16, 0.62, 0.85)  # 一个按下的键，暗示交互
			img.fill_rect(rect, col)
	img.fill_rect(Rect2i(200, 596, 830, 46), Color(0.12, 0.15, 0.21))
	img.save_png(path)
