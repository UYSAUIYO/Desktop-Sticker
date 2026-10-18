extends PanelContainer
## 图层面板：树形列表（容器的子层缩进显示在容器下面），顶部 = 最上层。
## 下标换算只在 _rows 里做一次：每一行对应一条**路径**（PackedInt32Array）。
##
## 添加图片图层时会把文件拷进工程 assets/（见 asset_import.gd）——文档里只存相对路径，
## 这样工程拷到别的机器上仍然能打开。

const Layer = preload("res://src/doc/layer.gd")
const AssetImport = preload("res://src/ui/asset_import.gd")

const TYPE_NAMES := {
	"color": "纯色",
	"image": "图片",
	"text": "文字",
	"particles": "粒子",
	"group": "容器",
}

var main = null

var _list: ItemList
var _empty_hint: Label
var _add_menu: MenuButton
var _btn_delete: Button
var _btn_up: Button
var _btn_down: Button
var _btn_visible: Button
var _btn_group: Button
var _btn_ungroup: Button
var _file_dialog: FileDialog
var _rows: Array = []        # [{path: PackedInt32Array, layer: Dictionary, depth: int}]


func setup(m) -> void:
	main = m


func _ready() -> void:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.105, 0.11, 0.125)
	sb.border_width_right = 1
	sb.border_color = Color(0.2, 0.21, 0.245)
	sb.content_margin_left = 10
	sb.content_margin_right = 10
	sb.content_margin_top = 8
	sb.content_margin_bottom = 8
	add_theme_stylebox_override("panel", sb)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)

	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", 6)
	box.add_child(header)

	var title := Label.new()
	title.text = "图层"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.add_theme_font_size_override("font_size", 13)
	title.modulate = Color(0.78, 0.81, 0.87)
	header.add_child(title)

	_add_menu = MenuButton.new()
	_add_menu.text = "＋ 添加"
	_add_menu.flat = true
	_add_menu.tooltip_text = "添加图层（选中容器时加进容器里）"
	var popup := _add_menu.get_popup()
	popup.add_item("纯色背景", 0)
	popup.add_item("图片", 1)
	popup.add_item("文字", 2)
	popup.add_item("粒子", 3)
	popup.add_item("容器 / 画布", 4)
	popup.id_pressed.connect(_on_add)
	header.add_child(_add_menu)

	_list = ItemList.new()
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.allow_reselect = true
	_list.item_selected.connect(_on_item_selected)
	box.add_child(_list)

	_empty_hint = Label.new()
	_empty_hint.text = "还没有图层\n点右上角「＋ 添加」"
	_empty_hint.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_empty_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_empty_hint.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_empty_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_empty_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_empty_hint.modulate = Color(1, 1, 1, 0.32)
	_empty_hint.add_theme_font_size_override("font_size", 12)
	_list.add_child(_empty_hint)

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 4)
	box.add_child(buttons)

	_btn_delete = _small_button(buttons, "删除", "删除选中图层（含容器里的内容）")
	_btn_up = _small_button(buttons, "上移", "向上（更靠前）移动一层")
	_btn_down = _small_button(buttons, "下移", "向下（更靠后）移动一层")
	_btn_visible = _small_button(buttons, "显示", "显示 / 隐藏选中图层")

	var buttons2 := HBoxContainer.new()
	buttons2.add_theme_constant_override("separation", 4)
	box.add_child(buttons2)
	_btn_group = _small_button(buttons2, "放进容器", "把选中图层包进一个新建的容器")
	_btn_ungroup = _small_button(buttons2, "移出容器", "把选中图层移出它的父容器")

	_btn_delete.pressed.connect(_on_delete)
	_btn_up.pressed.connect(_on_move.bind(1))
	_btn_down.pressed.connect(_on_move.bind(-1))
	_btn_visible.pressed.connect(_on_toggle_visible)
	_btn_group.pressed.connect(_on_wrap)
	_btn_ungroup.pressed.connect(_on_unwrap)

	_update_buttons()


func _small_button(parent: Control, text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	b.add_theme_font_size_override("font_size", 12)
	parent.add_child(b)
	return b


# ---------------------------------------------------------------- 列表

func refresh() -> void:
	if main == null or main.doc == null or _list == null:
		return
	_list.clear()
	_rows = []
	_collect(PackedInt32Array(), 0)
	for row in _rows:
		var layer: Dictionary = row["layer"]
		var type_name := String(TYPE_NAMES.get(String(layer.get("type", "color")), "图层"))
		var marks := ""
		if bool(layer.get("mask", {}).get("enabled", false)):
			marks += " ◐"
		var f: Dictionary = layer.get("filters", {})
		for key in f.keys():
			if bool((f[key] as Dictionary).get("enabled", false)):
				marks += " ✳"
				break
		var indent := "　".repeat(int(row["depth"]))
		var label := "%s%s    ·    %s%s" % [indent, String(layer.get("name", "图层")), type_name, marks]
		if not bool(layer.get("visible", true)):
			label = "（隐藏）" + label
		var idx := _list.add_item(label)
		if not bool(layer.get("visible", true)):
			_list.set_item_custom_fg_color(idx, Color(0.52, 0.54, 0.6))
		if Layer.is_group(layer):
			_list.set_item_custom_fg_color(idx, Color(0.78, 0.84, 0.95))
	if not _rows.is_empty() and main.sel.is_empty():
		# 默认选中最上层：打开工程后属性面板立刻有内容，不用先点一下。
		main.select_layer_path(_rows[0]["path"])
	_empty_hint.visible = _rows.is_empty()
	sync_selection()


## 递归收集行：同一个父级内从最上层往下，容器的子层紧跟在容器下面。
func _collect(parent_path: PackedInt32Array, depth: int) -> void:
	var arr: Array = main.doc.siblings_at(parent_path)
	for i in range(arr.size() - 1, -1, -1):
		var path := parent_path.duplicate()
		path.append(i)
		var layer: Dictionary = arr[i]
		_rows.append({"path": path, "layer": layer, "depth": depth})
		if Layer.is_group(layer):
			_collect(path, depth + 1)


func _row_of(path: PackedInt32Array) -> int:
	for i in _rows.size():
		if (_rows[i]["path"] as PackedInt32Array) == path:
			return i
	return -1


func sync_selection() -> void:
	if _list == null or main == null or main.doc == null:
		return
	var row := _row_of(main.sel)
	if row >= 0:
		_list.select(row)
	else:
		_list.deselect_all()
	_update_buttons()


func _update_buttons() -> void:
	if _list == null:
		return
	var layer: Dictionary = main.selected_layer() if main != null else {}
	var has := not layer.is_empty()
	var parent: PackedInt32Array = main.doc.parent_path_of(main.sel) if (main != null and main.doc != null) else PackedInt32Array()
	var siblings: Array = main.doc.siblings_at(parent) if (main != null and main.doc != null) else []
	var idx: int = main.sel[main.sel.size() - 1] if main != null and not main.sel.is_empty() else -1
	var path_size: int = 0 if (main == null or main.sel.is_empty()) else main.sel.size()
	_btn_delete.disabled = not has
	_btn_up.disabled = not has or idx < 0 or idx >= siblings.size() - 1
	_btn_down.disabled = not has or idx <= 0
	_btn_visible.disabled = not has
	_btn_group.disabled = not has
	_btn_ungroup.disabled = not has or path_size <= 1
	if has:
		_btn_visible.text = "隐藏" if bool(layer.get("visible", true)) else "显示"


func _on_item_selected(row: int) -> void:
	if row >= 0 and row < _rows.size():
		main.select_layer_path(_rows[row]["path"])


# ---------------------------------------------------------------- 增删改

func _on_add(id: int) -> void:
	if main == null or main.doc == null:
		return
	match id:
		0:
			_add_layer(Layer.make_color("纯色", Color(0.12, 0.34, 0.62)))
		1:
			_pick_image()
		2:
			_add_layer(Layer.make_text("文字", "文本"))
		3:
			_add_layer(Layer.make_particles("粒子", ""))
		4:
			_add_layer(Layer.make_group("容器"))


## 选中容器时把新层加进容器，否则加到根的最上层。
func _add_layer(layer: Dictionary) -> void:
	var parent := PackedInt32Array()
	var current: Dictionary = main.selected_layer()
	if not current.is_empty() and Layer.is_group(current):
		parent = main.sel.duplicate()
	var path: PackedInt32Array = main.doc.insert_layer(parent, layer)
	main.select_layer_path(path)
	main.apply_change(true)


func _on_delete() -> void:
	if main == null or main.doc == null or main.sel.is_empty():
		return
	var parent: PackedInt32Array = main.doc.parent_path_of(main.sel)
	main.doc.remove_at(main.sel)
	var siblings: Array = main.doc.siblings_at(parent)
	var path := PackedInt32Array()
	if not siblings.is_empty():
		path = parent.duplicate()
		path.append(siblings.size() - 1)
	main.select_layer_path(path)
	main.apply_change(true)


func _on_move(delta: int) -> void:
	if main == null or main.doc == null or main.sel.is_empty():
		return
	var moved: PackedInt32Array = main.doc.move_within(main.sel, delta)
	main.select_layer_path(moved)
	main.apply_change(true)


func _on_toggle_visible() -> void:
	var layer: Dictionary = main.selected_layer()
	if layer.is_empty():
		return
	layer["visible"] = not bool(layer.get("visible", true))
	main.apply_change(false)


func _on_wrap() -> void:
	if main == null or main.doc == null or main.sel.is_empty():
		return
	var path: PackedInt32Array = main.doc.wrap_in_group(main.sel)
	main.select_layer_path(path)
	main.apply_change(true)


func _on_unwrap() -> void:
	if main == null or main.doc == null or main.sel.is_empty():
		return
	var grand: PackedInt32Array = main.doc.parent_path_of(main.doc.parent_path_of(main.sel))
	var path: PackedInt32Array = main.doc.reparent(main.sel, grand)
	main.select_layer_path(path)
	main.apply_change(true)


# ---------------------------------------------------------------- 图片导入

func _pick_image() -> void:
	var dir: String = main.ensure_project_dir()
	if dir.is_empty():
		main.show_error("无法添加图片", "工程目录不可写。")
		return
	if _file_dialog == null:
		_file_dialog = FileDialog.new()
		_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_file_dialog.title = "选择图片（会拷进工程 assets/）"
		_file_dialog.filters = AssetImport.image_filters()
		_file_dialog.file_selected.connect(_on_image_selected)
		add_child(_file_dialog)
	_file_dialog.current_dir = dir
	_file_dialog.popup_centered_ratio(0.7)


func _on_image_selected(path: String) -> void:
	var rel := AssetImport.import_image(main.ensure_project_dir(), path)
	if rel.is_empty():
		main.show_error("添加图片失败", "无法把图片拷进工程：\n" + path)
		return
	_add_layer(Layer.make_image(path.get_file().get_basename(), rel))
