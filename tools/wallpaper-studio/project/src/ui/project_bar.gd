extends PanelContainer
## 顶部工具条：新建 / 打开 / 保存 / 导出。所有会阻塞的操作（导出、投递）都在这里，
## 面板只负责文档本身。
##
## 导出流程刻意拆成两步：先 await 渲染海报（需要两帧 + frame_post_draw），
## 再把海报路径交给**同步的** Exporter.export。Exporter 内部不 await，
## 所以整条导出链路也能被无头测试直接调用。

const StudioPaths = preload("res://src/doc/paths.gd")
const Poster = preload("res://src/export/poster.gd")
const Exporter = preload("res://src/export/exporter.gd")

const RESOLUTIONS := [
	Vector2i(1920, 1080),
	Vector2i(2560, 1440),
	Vector2i(3840, 2160),
]

var main = null

var _info: Label
var _status: Label
var _btn_undo: Button
var _btn_redo: Button
var _buttons: Array = []
var _overlay: ColorRect
var _overlay_label: Label
var _open_dialog: FileDialog
var _export_dialog: FileDialog
var _busy := false


func setup(m) -> void:
	main = m
	_overlay = ColorRect.new()
	_overlay.color = Color(0.03, 0.04, 0.06, 0.55)
	_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	_overlay.visible = false

	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_overlay_label = Label.new()
	_overlay_label.text = "正在导出…"
	_overlay_label.add_theme_font_size_override("font_size", 18)
	center.add_child(_overlay_label)
	_overlay.add_child(center)

	# 加在 main 的最后一个子节点：盖住工具条、图层、画布、属性面板。
	main.add_child(_overlay)


func _ready() -> void:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.125, 0.133, 0.152)
	sb.border_width_bottom = 1
	sb.border_color = Color(0.22, 0.235, 0.27)
	sb.content_margin_left = 12
	sb.content_margin_right = 12
	sb.content_margin_top = 4
	sb.content_margin_bottom = 4
	add_theme_stylebox_override("panel", sb)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	add_child(row)

	var brand := Label.new()
	brand.text = "壁纸编辑器"
	brand.add_theme_font_size_override("font_size", 13)
	brand.modulate = Color(0.82, 0.86, 0.93)
	row.add_child(brand)

	var sep := VSeparator.new()
	row.add_child(sep)

	_btn_undo = _button(row, "撤销", "撤销上一步（Ctrl+Z）", func() -> void: main.undo())
	_btn_redo = _button(row, "重做", "重做（Ctrl+Shift+Z）", func() -> void: main.redo())
	sync_history()

	_info = Label.new()
	_info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_info.add_theme_font_size_override("font_size", 12)
	_info.modulate = Color(0.62, 0.66, 0.74)
	_info.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	row.add_child(_info)

	_status = Label.new()
	_status.add_theme_font_size_override("font_size", 12)
	row.add_child(_status)

	_button(row, "新建", "新建壁纸（Ctrl+N）", popup_new)
	_button(row, "打开", "打开工程目录（Ctrl+O）", popup_open)
	_button(row, "保存", "保存 wallpaper.json（Ctrl+S）", _on_save)
	_button(row, "导出", "导出 .dswall（Ctrl+E）", popup_export)

	var apply := _button(row, "导出并应用到桌面", "导出后投递到桌面贴纸收件箱，自动热应用", export_apply)
	apply.add_theme_color_override("font_color", Color(0.6, 0.85, 0.95))


func _button(parent: Control, text: String, tip: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.custom_minimum_size = Vector2(0, 26)
	b.add_theme_font_size_override("font_size", 12)
	b.pressed.connect(cb)
	parent.add_child(b)
	_buttons.append(b)
	return b


func sync_history() -> void:
	if _btn_undo == null or _btn_redo == null or main == null:
		return
	_btn_undo.disabled = not main.can_undo()
	_btn_redo.disabled = not main.can_redo()


func update_status() -> void:
	if _info == null:
		return
	if main == null or main.doc == null:
		_info.text = ""
		_status.text = ""
		return
	_info.text = "%s    ·    %d × %d" % [
		String(main.doc.name), main.doc.canvas.x, main.doc.canvas.y,
	]
	# 新建但一次都没保存过的工程没有目录，不能显示成「已保存」。
	var saved: bool = not main.dirty and not String(main.project_dir).is_empty()
	_status.text = "已保存" if saved else "未保存"
	_status.modulate = Color(0.52, 0.72, 0.54) if saved else Color(1.0, 0.78, 0.35)


# ---------------------------------------------------------------- 新建

func popup_new() -> void:
	if _busy:
		return
	var d := ConfirmationDialog.new()
	d.title = "新建壁纸"
	d.ok_button_text = "创建"
	d.cancel_button_text = "取消"
	d.min_size = Vector2i(440, 0)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	box.custom_minimum_size = Vector2(400, 0)
	d.add_child(box)

	var name_edit := LineEdit.new()
	name_edit.text = "未命名壁纸"
	box.add_child(_labeled("名称", name_edit))

	var preset := OptionButton.new()
	preset.add_item("1920 × 1080（全高清）", 0)
	preset.add_item("2560 × 1440（2K）", 1)
	preset.add_item("3840 × 2160（4K）", 2)
	preset.add_item("自定义", 3)
	preset.select(0)
	box.add_child(_labeled("分辨率", preset))

	var mode := OptionButton.new()
	mode.add_item("2D 图层场景（平面动画）", 0)
	mode.add_item("3D 场景（物体 + 相机 + 光）", 1)
	mode.select(0)
	box.add_child(_labeled("场景类型", mode))

	var size_row := HBoxContainer.new()
	size_row.add_theme_constant_override("separation", 6)
	var width := SpinBox.new()
	width.min_value = 64
	width.max_value = 7680
	width.step = 1
	width.value = 1920
	width.editable = false
	width.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var times := Label.new()
	times.text = "×"
	var height := SpinBox.new()
	height.min_value = 64
	height.max_value = 4320
	height.step = 1
	height.value = 1080
	height.editable = false
	height.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_row.add_child(width)
	size_row.add_child(times)
	size_row.add_child(height)
	box.add_child(size_row)

	preset.item_selected.connect(func(i: int) -> void:
		var custom := i >= RESOLUTIONS.size()
		width.editable = custom
		height.editable = custom
		if not custom:
			width.value = RESOLUTIONS[i].x
			height.value = RESOLUTIONS[i].y
	)

	d.register_text_enter(name_edit)
	d.canceled.connect(d.queue_free)
	d.close_requested.connect(d.queue_free)
	d.confirmed.connect(func() -> void:
		var title := name_edit.text.strip_edges()
		if title.is_empty():
			title = "未命名壁纸"
		var size := Vector2i(int(width.value), int(height.value))
		var scene_mode := "3d" if mode.selected == 1 else "2d"
		d.queue_free()
		main.request_new_project(title, size, scene_mode)
	)
	add_child(d)
	d.popup_centered()


func _labeled(text: String, control: Control) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	var lab := Label.new()
	lab.text = text
	lab.custom_minimum_size = Vector2(64, 0)
	lab.add_theme_font_size_override("font_size", 12)
	lab.modulate = Color(0.72, 0.75, 0.82)
	row.add_child(lab)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)
	return row


# ---------------------------------------------------------------- 打开

func popup_open() -> void:
	if _busy:
		return
	if _open_dialog == null:
		_open_dialog = FileDialog.new()
		_open_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
		_open_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_open_dialog.title = "打开工程（选择包含 wallpaper.json 的目录）"
		_open_dialog.dir_selected.connect(func(path: String) -> void:
			main.request_open_project(path)
		)
		add_child(_open_dialog)
	_open_dialog.current_dir = ProjectSettings.globalize_path(StudioPaths.default_project_root())
	_open_dialog.popup_centered_ratio(0.7)


func _on_save() -> void:
	if _busy:
		return
	main.save_project()


# ---------------------------------------------------------------- 导出

func popup_export() -> void:
	if _busy or main.doc == null:
		return
	if _export_dialog == null:
		_export_dialog = FileDialog.new()
		_export_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
		_export_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_export_dialog.title = "导出 .dswall"
		_export_dialog.filters = PackedStringArray(["*.dswall ; 壁纸包"])
		_export_dialog.file_selected.connect(func(path: String) -> void:
			if not path.to_lower().ends_with(".dswall"):
				path += ".dswall"
			_start_export(path, false)
		)
		add_child(_export_dialog)
	var dir := ProjectSettings.globalize_path(StudioPaths.exports_dir())
	DirAccess.make_dir_recursive_absolute(dir)
	_export_dialog.current_dir = dir
	_export_dialog.current_file = StudioPaths.dswall_file_name(String(main.doc.name))
	_export_dialog.popup_centered_ratio(0.7)


## 一键路径：导出到 user://exports/<名字>.dswall，成功后立刻投递到桌面贴纸收件箱。
func export_apply() -> void:
	if _busy or main.doc == null:
		return
	var dir := ProjectSettings.globalize_path(StudioPaths.exports_dir())
	DirAccess.make_dir_recursive_absolute(dir)
	var path := dir.path_join(StudioPaths.dswall_file_name(String(main.doc.name)))
	_start_export(path, true)


func _start_export(out_path: String, deliver: bool) -> void:
	if _busy or main.doc == null:
		return
	if main.ensure_project_dir().is_empty():
		main.show_error("还不能导出", "无法创建工程目录（user:// 不可写？）。")
		return
	var problems: PackedStringArray = main.doc.validate(main.project_dir)
	if not problems.is_empty():
		main.show_errors("还不能导出", problems)
		return

	_busy = true
	main.busy = true
	# 导出期间不接受编辑：先把焦点收回，再盖上覆盖层（覆盖层只挡鼠标）。
	get_viewport().gui_release_focus()
	_set_busy(true, "正在渲染海报…")

	var scratch := ProjectSettings.globalize_path(StudioPaths.scratch_dir(String(main.doc.pack_id)))
	DirAccess.make_dir_recursive_absolute(scratch)
	var poster_path := scratch.path_join("poster.png")
	var poster_ok := await Poster.render(main.doc, main.project_dir, poster_path, get_tree())
	if not poster_ok:
		# 海报只是壁纸列表里的缩略图，渲染失败不该阻塞导出 —— 但包里就没有它了。
		poster_path = ""

	_set_busy(true, "正在打包…")
	await get_tree().process_frame
	var result: Dictionary = Exporter.export(main.doc, main.project_dir, out_path, poster_path)

	_busy = false
	main.busy = false
	_set_busy(false, "")

	if not result.ok:
		main.show_error("导出失败", String(result.error))
		return

	if not deliver:
		main.show_message("导出成功", String(result.path))
		return

	var sent: Dictionary = Exporter.deliver_to_inbox(String(result.path))
	if sent.ok:
		main.show_message("已应用到桌面",
				"壁纸已投递到桌面贴纸：\n%s\n\n桌面贴纸会在几秒内自动导入并应用。" % String(sent.path))
	else:
		main.show_message("已导出，但投递失败",
				"文件已导出到：\n%s\n\n投递失败：%s\n\n可手动导入这个 .dswall。" % [
					String(result.path), String(sent.error),
				])


func _set_busy(on: bool, text: String) -> void:
	if _overlay == null:
		return
	_overlay.visible = on
	if _overlay_label != null:
		_overlay_label.text = text
	for b in _buttons:
		(b as Button).disabled = on
