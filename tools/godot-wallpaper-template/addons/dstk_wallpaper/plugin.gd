@tool
extends EditorPlugin
## 桌面贴纸场景壁纸插件：
##   - 一个常驻「壁纸」停靠面板：项目信息 / 能力声明 / 校验 / 导出 / 一键应用到桌面；
##   - 「导出壁纸包…」：把工程导出为 .dswall（ZIP：project.pck + manifest.json + poster.png）；
##   - 「导出并应用到桌面」：导出后原子投递到桌面贴纸收件箱，运行中的实例立即换上。
##
## .dswall 由桌面贴纸壁纸模块导入；运行时由内嵌的 Godot 运行库加载 project.pck 渲染，
## 用户机器上不需要安装/启动任何 Godot 程序。
## 作者元数据与 pack_id 存在工程根的 dstk_wallpaper.json 里（可版本管理）。

const PRESET_NAME := "WallpaperPack"
const PRESET_FILE := "res://export_presets.cfg"
const CONFIG_FILE := "res://dstk_wallpaper.json"
const TEMPLATE_2D := "res://wallpaper_2d.tscn"
const TEMPLATE_3D := "res://wallpaper_3d.tscn"
const ENTRY_PACK := "project.pck"
const ENTRY_MANIFEST := "manifest.json"
const ENTRY_POSTER := "poster.png"
const FORMAT_VERSION := 1

const DockScript := preload("res://addons/dstk_wallpaper/wallpaper_dock.gd")

var _dock = null

func _enter_tree() -> void:
	add_tool_menu_item("校验壁纸场景", _run_validation_dialog)
	add_tool_menu_item("导出壁纸包…", func(): _start_export(false))

	_dock = DockScript.new()
	_dock.set_name("DesktopStickerWallpaper")
	add_control_to_dock(DOCK_SLOT_LEFT_BL, _dock)
	_dock.validate_requested.connect(_run_validation_panel)
	_dock.export_requested.connect(_start_export)
	_dock.fields_changed.connect(_save_config)
	_dock.template_requested.connect(_load_template)
	_dock.set_fields(_load_config())

func _exit_tree() -> void:
	remove_tool_menu_item("校验壁纸场景")
	remove_tool_menu_item("导出壁纸包…")
	if _dock:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null

# ---------------------------------------------------------------------------
# 工程配置（dstk_wallpaper.json）：作者元数据 + 稳定 pack_id
# ---------------------------------------------------------------------------

func _load_config() -> Dictionary:
	var cfg := {
		"title": ProjectSettings.get_setting("application/config/name", ""),
		"author": "",
		"description": "",
		"tags": PackedStringArray(),
		"categories": PackedStringArray(),
		"requested_permissions": PackedStringArray(),
		"pack_id": "",
		"dimension": "3d",
	}
	if FileAccess.file_exists(CONFIG_FILE):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(CONFIG_FILE))
		if typeof(parsed) == TYPE_DICTIONARY:
			for k in cfg.keys():
				if parsed.has(k):
					cfg[k] = parsed[k]
	# pack_id 一旦生成就固定写回，供桌面贴纸热应用去重（同作者包原地替换）。
	if String(cfg["pack_id"]).is_empty():
		cfg["pack_id"] = _make_pack_id()
		_write_config(cfg)
	return cfg

func _save_config() -> void:
	if _dock == null:
		return
	var cfg := _load_config()          # 保留 pack_id
	cfg.merge(_dock.get_fields())
	_write_config(cfg)

func _write_config(cfg: Dictionary) -> void:
	var f := FileAccess.open(CONFIG_FILE, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(cfg, "  "))

func _make_pack_id() -> String:
	var bytes := Crypto.new().generate_random_bytes(16)
	var s := ""
	for b in bytes:
		s += "%02x" % b
	return s

# 载入起始模板：把选定的 2D/3D 场景设为主场景并打开编辑，同时记住维度。
func _load_template(dimension: String) -> void:
	var scene := TEMPLATE_2D if dimension == "2d" else TEMPLATE_3D
	if not ResourceLoader.exists(scene):
		_report("缺少起始模板：%s（请确认模板工程文件完整）。" % scene)
		return
	ProjectSettings.set_setting("application/run/main_scene", scene)
	ProjectSettings.save()
	var cfg := _load_config()
	cfg["dimension"] = dimension
	_write_config(cfg)
	if _dock:
		_dock.set_fields(cfg)
		_dock.set_status("已载入 %s 起始模板为主场景。\n可在此基础上继续编辑。" % ("2D" if dimension == "2d" else "3D"))
	get_editor_interface().edit_scene_from_path(scene)

func _current_fields() -> Dictionary:
	return _dock.get_fields() if _dock else _load_config()

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
	var info := {"cameras": 0, "has_3d": false, "has_canvas": false}
	_scan_scene(root, info)
	root.free()

	if bool(info.has_3d):
		# 3D 场景：必须有相机才能看到画面。
		if int(info.cameras) == 0:
			problems.append("3D 场景里没有 Camera3D：壁纸会看不到画面。")
	elif bool(info.has_canvas):
		# 纯 2D（Control/Node2D）：无需相机即可渲染，不报错。
		pass
	else:
		problems.append("场景里既没有 3D 也没有 2D 可见内容，壁纸会是空屏。")
	return problems

func _scan_scene(node: Node, info: Dictionary) -> void:
	if node is Camera3D:
		info.cameras = int(info.cameras) + 1
	if node is Node3D:
		info.has_3d = true
	if node is CanvasItem:
		info.has_canvas = true
	for child in node.get_children():
		_scan_scene(child, info)

func _run_validation_panel() -> void:
	var problems := _collect_problems()
	_dock.set_problems(problems)
	if problems.is_empty():
		print("[dstk] 校验通过。")
	else:
		print("[dstk] 校验发现问题：")
		for p in problems:
			print("  - " + p)

func _run_validation_dialog() -> void:
	var problems := _collect_problems()
	if _dock:
		_dock.set_problems(problems)
	if problems.is_empty():
		OS.alert("校验通过：场景可以导出为壁纸包。", "桌面贴纸 · 壁纸校验")
	else:
		OS.alert("校验未通过：\n\n- " + "\n- ".join(problems), "桌面贴纸 · 壁纸校验")

# ---------------------------------------------------------------------------
# 导出
# ---------------------------------------------------------------------------

func _start_export(apply: bool) -> void:
	var problems := _collect_problems()
	if not problems.is_empty():
		if _dock:
			_dock.set_problems(problems)
			_dock.set_status("校验未通过，已在校验结果里列出问题，请先修复。")
		_run_validation_dialog()
		return
	if not FileAccess.file_exists(PRESET_FILE):
		_report("缺少 %s：导出预设丢失，请从模板重新复制该文件。" % PRESET_FILE)
		return

	if apply:
		# 热应用不需要用户选路径：导出到临时目录再投递收件箱。
		var out_path := OS.get_temp_dir().path_join(
			"%s.dswall" % ProjectSettings.get_setting("application/config/name", "wallpaper"))
		_export_to(out_path, null, true)
		return

	var dialog := EditorFileDialog.new()
	dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	dialog.add_filter("*.dswall", "桌面贴纸壁纸包")
	dialog.current_file = "%s.dswall" % ProjectSettings.get_setting("application/config/name", "wallpaper")
	dialog.file_selected.connect(_export_to.bind(dialog, false))
	dialog.canceled.connect(dialog.queue_free)
	get_editor_interface().get_base_control().add_child(dialog)
	dialog.popup_centered_ratio(0.6)

func _export_to(out_path: String, dialog, apply: bool) -> void:
	if dialog:
		dialog.queue_free()
	if _dock:
		_dock.set_status("正在导出…")

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
		_report("导出 PCK 失败（exit %d）。详情见编辑器输出。" % code)
		return

	# 2) 海报：离屏渲染主场景一帧。
	await _render_poster(poster_path)

	# 3) manifest（含作者元数据、pack_id、能力声明）。
	_write_manifest(manifest_path)

	# 4) 打包成 .dswall（ZIP）。
	var zip := ZIPPacker.new()
	if zip.open(out_path) != OK:
		_report("无法写入：%s" % out_path)
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

	if not ok:
		_report("打包 .dswall 失败：%s" % out_path)
		return

	print("[dstk] 导出完成：", out_path)
	if apply:
		if _deliver_to_inbox(out_path):
			_report_ok("已导出并投递到桌面贴纸收件箱，稍候即会换上：\n%s" % out_path)
		else:
			_report("已导出到 %s，但投递收件箱失败（桌面贴纸的壁纸目录不可写？）。" % out_path)
	else:
		_report_ok("导出完成：\n%s\n\n在桌面贴纸的壁纸设置里导入这个文件即可。" % out_path)

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
	var fields := _current_fields()
	var manifest := {
		"format_version": FORMAT_VERSION,
		"title": String(fields.get("title", "")),
		"author": String(fields.get("author", "")),
		"description": String(fields.get("description", "")),
		"pack_id": String(_load_config().get("pack_id", "")),
		"dimension": String(_load_config().get("dimension", "3d")),
		"tags": Array(fields.get("tags", [])),
		"categories": Array(fields.get("categories", [])),
		"main_scene": _main_scene_path(),
		"engine": {
			"version": "%d.%d.%d" % [vi.major, vi.minor, vi.patch],
			"build": vi.string,
		},
		"created_utc": Time.get_datetime_string_from_system(true, true),
		# M3 权限机制：导出时声明这个壁纸请求的能力，导入端逐项授权。
		"requested_permissions": Array(fields.get("requested_permissions", [])),
	}
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(manifest, "  "))

func _zip_add_file(zip: ZIPPacker, name: String, disk_path: String) -> bool:
	var bytes := FileAccess.get_file_as_bytes(disk_path)
	if bytes.is_empty():
		return false
	zip.start_file(name)
	return zip.write_file(bytes) == OK

# ---------------------------------------------------------------------------
# 热应用投递：原子写入桌面贴纸收件箱（先 .tmp 再改名，模块只认 .dswall）
# ---------------------------------------------------------------------------

func _inbox_dir() -> String:
	var appdata := OS.get_environment("APPDATA")
	if appdata.is_empty():
		return ""
	return appdata.path_join("DesktopSticker").path_join("wallpaper_inbox")

func _deliver_to_inbox(dswall_path: String) -> bool:
	var inbox := _inbox_dir()
	if inbox.is_empty():
		return false
	if DirAccess.make_dir_recursive_absolute(inbox) != OK:
		return false

	var fname := dswall_path.get_file()
	var final := inbox.path_join(fname)
	var tmp := inbox.path_join(fname + ".tmp")

	var bytes := FileAccess.get_file_as_bytes(dswall_path)
	if bytes.is_empty():
		return false
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()

	if FileAccess.file_exists(final):
		DirAccess.remove_absolute(final)
	return DirAccess.rename_absolute(tmp, final) == OK

# ---------------------------------------------------------------------------
# 反馈
# ---------------------------------------------------------------------------

func _report(text: String) -> void:
	printerr("[dstk] " + text)
	if _dock:
		_dock.set_status(text)
	OS.alert(text, "桌面贴纸 · 导出壁纸包")

func _report_ok(text: String) -> void:
	print("[dstk] " + text)
	if _dock:
		_dock.set_status(text)
