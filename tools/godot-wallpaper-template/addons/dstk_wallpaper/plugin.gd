@tool
extends EditorPlugin
## 桌面贴纸场景壁纸插件：
##   - 「校验壁纸场景」：检查主场景/相机/3D 内容；
##   - 「导出壁纸包…」：把工程导出为 .dswall（ZIP：project.pck + manifest.json + poster.png）。
##
## .dswall 由桌面贴纸壁纸模块导入；运行时由内嵌的 Godot 运行库加载 project.pck 渲染，
## 用户机器上不需要安装/启动任何 Godot 程序。

const PRESET_NAME := "WallpaperPack"
const PRESET_FILE := "res://export_presets.cfg"
const ENTRY_PACK := "project.pck"
const ENTRY_MANIFEST := "manifest.json"
const ENTRY_POSTER := "poster.png"
const FORMAT_VERSION := 1

func _enter_tree() -> void:
	add_tool_menu_item("校验壁纸场景", _run_validation)
	add_tool_menu_item("导出壁纸包…", _choose_export_path)

func _exit_tree() -> void:
	remove_tool_menu_item("校验壁纸场景")
	remove_tool_menu_item("导出壁纸包…")

# ---------------------------------------------------------------------------
# 校验
# ---------------------------------------------------------------------------

func _main_scene_path() -> String:
	return str(ProjectSettings.get_setting("application/run/main_scene", ""))

func _collect_problems() -> PackedStringArray:
	var problems := PackedStringArray()
	var scene_path := _main_scene_path()
	if scene_path.is_empty():
		problems.append("没有设置主场景（项目设置 → 应用 → 运行 → 主场景）。")
		return problems
	if not ResourceLoader.exists(scene_path):
		problems.append("主场景不存在：%s" % scene_path)
		return problems

	var packed := load(scene_path) as PackedScene
	if packed == null:
		problems.append("主场景无法加载：%s（先修复脚本/资源报错）" % scene_path)
		return problems

	var root := packed.instantiate()
	var info := {"cameras": 0, "has_3d": false}
	_scan_scene(root, info)
	root.free()

	if int(info.cameras) == 0:
		problems.append("场景里没有 Camera3D：壁纸会看不到画面。")
	if not bool(info.has_3d):
		problems.append("场景里没有 3D 节点：这个模板面向 3D 场景壁纸。")
	return problems

func _scan_scene(node: Node, info: Dictionary) -> void:
	if node is Camera3D:
		info.cameras = int(info.cameras) + 1
	if node is Node3D:
		info.has_3d = true
	for child in node.get_children():
		_scan_scene(child, info)

func _run_validation() -> void:
	var problems := _collect_problems()
	if problems.is_empty():
		print("[dstk] 校验通过：场景可以导出为壁纸包。")
		OS.alert("校验通过：场景可以导出为壁纸包。", "桌面贴纸 · 壁纸校验")
	else:
		print("[dstk] 校验发现问题：")
		for p in problems:
			print("  - " + p)
		OS.alert("校验未通过：\n\n- " + "\n- ".join(problems), "桌面贴纸 · 壁纸校验")

# ---------------------------------------------------------------------------
# 导出
# ---------------------------------------------------------------------------

func _choose_export_path() -> void:
	var problems := _collect_problems()
	if not problems.is_empty():
		_run_validation()
		return
	if not FileAccess.file_exists(PRESET_FILE):
		OS.alert("缺少 %s：导出预设丢失，请从模板重新复制该文件。" % PRESET_FILE, "桌面贴纸 · 导出壁纸包")
		return

	var dialog := EditorFileDialog.new()
	dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	dialog.add_filter("*.dswall", "桌面贴纸壁纸包")
	dialog.current_file = "%s.dswall" % ProjectSettings.get_setting("application/config/name", "wallpaper")
	dialog.file_selected.connect(_export_to.bind(dialog))
	dialog.canceled.connect(dialog.queue_free)
	get_editor_interface().get_base_control().add_child(dialog)
	dialog.popup_centered_ratio(0.6)

func _export_to(out_path: String, dialog: EditorFileDialog) -> void:
	dialog.queue_free()

	var tmp_dir := OS.get_temp_dir().path_join("dstk_wallpaper_export")
	DirAccess.make_dir_recursive_absolute(tmp_dir)
	var pck_path := tmp_dir.path_join(ENTRY_PACK)
	var manifest_path := tmp_dir.path_join(ENTRY_MANIFEST)
	var poster_path := tmp_dir.path_join(ENTRY_POSTER)
	for p in [pck_path, manifest_path, poster_path]:
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(p)

	# 1) 用编辑器自己的 CLI 导出 PCK（等价 CI 的 --export-pack，不依赖导出模板）。
	var proj_dir := ProjectSettings.globalize_path("res://")
	var args := ["--headless", "--path", proj_dir, "--export-pack", PRESET_NAME, pck_path]
	print("[dstk] 导出 PCK：", " ".join(args))
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), args, output, true)
	for line in output:
		print("  | ", line)
	if code != 0 or not FileAccess.file_exists(pck_path):
		OS.alert("导出 PCK 失败（exit %d）。\n详情见编辑器输出。\n\n提示：本工程需要由 Desktop Sticker 的 Godot 编辑器打开。" % code, "桌面贴纸 · 导出壁纸包")
		return

	# 2) 海报：离屏渲染主场景一帧。
	await _render_poster(poster_path)

	# 3) manifest。
	_write_manifest(manifest_path)

	# 4) 打包成 .dswall（ZIP）。
	var zip := ZIPPacker.new()
	if zip.open(out_path) != OK:
		OS.alert("无法写入：%s" % out_path, "桌面贴纸 · 导出壁纸包")
		return
	var ok := true
	ok = _zip_add_file(zip, ENTRY_PACK, pck_path) and ok
	ok = _zip_add_file(zip, ENTRY_MANIFEST, manifest_path) and ok
	if FileAccess.file_exists(poster_path):
		ok = _zip_add_file(zip, ENTRY_POSTER, poster_path) and ok
	zip.close()

	for p in [pck_path, manifest_path, poster_path]:
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(p)

	if ok:
		print("[dstk] 导出完成：", out_path)
		OS.alert("导出完成：\n%s\n\n在桌面贴纸的壁纸设置里导入这个文件即可。" % out_path, "桌面贴纸 · 导出壁纸包")
	else:
		OS.alert("打包 .dswall 失败：%s" % out_path, "桌面贴纸 · 导出壁纸包")

func _render_poster(path: String) -> void:
	var packed := load(_main_scene_path()) as PackedScene
	if packed == null:
		return
	var sub := SubViewport.new()
	sub.size = Vector2i(960, 540)
	sub.own_world_3d = true
	sub.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	get_editor_interface().get_base_control().add_child(sub)
	var inst := packed.instantiate()
	sub.add_child(inst)

	await get_tree().process_frame
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var img := sub.get_texture().get_image()
	sub.queue_free()
	if img != null:
		img.save_png(path)

func _write_manifest(path: String) -> void:
	var vi := Engine.get_version_info()
	var manifest := {
		"format_version": FORMAT_VERSION,
		"title": ProjectSettings.get_setting("application/config/name", ""),
		"main_scene": _main_scene_path(),
		"engine": {
			"version": "%d.%d.%d" % [vi.major, vi.minor, vi.patch],
			"build": vi.string,
		},
		"created_utc": Time.get_datetime_string_from_system(true, true),
		# M3 权限机制：导出时声明这个壁纸请求的能力，导入端逐项授权。
		"requested_permissions": [],
	}
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(manifest, "  "))

func _zip_add_file(zip: ZIPPacker, name: String, disk_path: String) -> bool:
	var bytes := FileAccess.get_file_as_bytes(disk_path)
	if bytes.is_empty():
		return false
	zip.start_file(name)
	return zip.write_file(bytes) == OK
