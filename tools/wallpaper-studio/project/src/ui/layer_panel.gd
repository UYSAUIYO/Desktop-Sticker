extends PanelContainer
## 图层面板：树形列表（容器的子层缩进显示在容器下面），顶部 = 最上层。
## 下标换算只在 _rows 里做一次：每一行对应一条**路径**（PackedInt32Array）。
##
## 添加图片图层时会把文件拷进工程 assets/（见 asset_import.gd）——文档里只存相对路径，
## 这样工程拷到别的机器上仍然能打开。

const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")
const AssetImport = preload("res://src/ui/asset_import.gd")
const LayerThumbs = preload("res://src/ui/layer_thumbs.gd")

const TYPE_NAMES := {
	"color": "纯色",
	"image": "图片",
	"text": "文字",
	"particles": "粒子",
	"group": "容器",
	"mask": "遮罩",
	"filter": "滤镜",
}
const MASK_ROW_COLOR := Color(0.93, 0.76, 0.35)
const FILTER_ROW_COLOR := Color(0.64, 0.86, 0.74)

var main = null

var _list: ItemList
var _empty_hint: Label
var _add_menu: MenuButton
var _btn_delete: Button
var _btn_up: Button
var _btn_down: Button
var _btn_visible: Button
var _btn_group: Button
var _btn_copy: Button
var _btn_ungroup: Button
var _file_dialog: FileDialog
var _rows: Array = []        # [{path: PackedInt32Array, layer: Dictionary, depth: int}]
## 折叠起来的容器（按图层 id 记：行下标会随增删移动，id 不会）。
var _folded: Dictionary = {}
var _thumbs = null
var _syncing := false


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
	popup.add_separator()
	popup.add_item("遮罩层（作用在下方图层）", 5)
	popup.add_item("滤镜层（作用在下方图层）", 6)
	popup.id_pressed.connect(_on_add)
	header.add_child(_add_menu)

	_list = ItemList.new()
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.allow_reselect = true
	_list.fixed_icon_size = Vector2i(24, 24)
	_list.item_selected.connect(_on_item_selected)
	_list.gui_input.connect(_on_list_input)
	box.add_child(_list)
	_thumbs = LayerThumbs.new()

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
	_btn_copy = _small_button(buttons2, "复制", "原地复制选中图层（Ctrl+D）")
	_btn_group = _small_button(buttons2, "放进容器", "把选中图层包进一个新建的容器")
	_btn_ungroup = _small_button(buttons2, "移出容器", "把选中图层移出它的父容器")

	_btn_delete.pressed.connect(_on_delete)
	_btn_up.pressed.connect(_on_move.bind(1))
	_btn_down.pressed.connect(_on_move.bind(-1))
	_btn_visible.pressed.connect(_on_toggle_visible)
	_btn_copy.pressed.connect(func() -> void: main.duplicate_selection())
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
		var path: PackedInt32Array = row["path"]
		var depth := int(row["depth"])
		var indent := "　".repeat(depth)
		var mark := ""
		if Layer.is_group(layer):
			mark = "▾ " if not _is_folded(layer) else "▸ "
		var label := indent + mark + _label_for(layer, path)
		if not bool(layer.get("visible", true)):
			label = "（隐藏）" + label
		var idx := _list.add_item(label, _thumbs.icon_for(layer, main.project_dir))
		if not bool(layer.get("visible", true)):
			_list.set_item_custom_fg_color(idx, Color(0.52, 0.54, 0.6))
		elif Layer.is_mask(layer):
			_list.set_item_custom_fg_color(idx, MASK_ROW_COLOR)
		elif Layer.is_filter(layer):
			_list.set_item_custom_fg_color(idx, FILTER_ROW_COLOR)
		elif Layer.is_group(layer):
			_list.set_item_custom_fg_color(idx, Color(0.78, 0.84, 0.95))
	if not _rows.is_empty() and main.sel.is_empty():
		# 默认选中最上层：打开工程后属性面板立刻有内容，不用先点一下。
		main.select_layer_path(_rows[0]["path"])
	_empty_hint.visible = _rows.is_empty()
	sync_selection()


## 一行文字：名称 · 类型；效果层额外标出"覆盖哪些层"。
func _label_for(layer: Dictionary, path: PackedInt32Array) -> String:
	var type_name := String(TYPE_NAMES.get(String(layer.get("type", "color")), "图层"))
	var out := "%s    ·    %s" % [String(layer.get("name", "图层")), type_name]
	if not Layer.is_effect(layer):
		if Layer.is_group(layer):
			var kids = layer.get("children", [])
			out += "    ·    %d 层" % (kids as Array).size()
		return out
	var target_id := Layer.cover_target(layer)
	if not target_id.is_empty():
		var target: Dictionary = {}
		if main != null and main.doc != null:
			target = main.doc.layer_at(main.doc.path_of_id(target_id))
		if target.is_empty():
			out += "    ·    绑定（目标已删除）"
		else:
			out += "    ·    绑定「%s」" % String(target.get("name", "图层"))
	elif String(layer.get("cover", {}).get("mode", "all")) == "layer":
		out += "    ·    绑定（未选择目标）"
	else:
		var n: int = main.doc.covered_count(path) if main != null and main.doc != null else 0
		out += "    ·    覆盖%s（共 %d 层）" % [Layer.cover_label(layer), n]
	if Layer.is_filter(layer) and not Filters.any(layer.get("filters", {})):
		out += "    ·    未启用"
	elif Layer.is_filter(layer):
		out += "    ·    固定范围" if Filters.region_enabled(layer) else "    ·    跟随内容"
	return out


## 递归收集行：同一个父级内从最上层往下，容器的子层紧跟在容器下面。
func _collect(parent_path: PackedInt32Array, depth: int) -> void:
	var arr: Array = main.doc.siblings_at(parent_path)
	for i in range(arr.size() - 1, -1, -1):
		var path := parent_path.duplicate()
		path.append(i)
		var layer: Dictionary = arr[i]
		_rows.append({"path": path, "layer": layer, "depth": depth})
		if Layer.is_group(layer) and not _is_folded(layer):
			_collect(path, depth + 1)


func _row_of(path: PackedInt32Array) -> int:
	for i in _rows.size():
		if (_rows[i]["path"] as PackedInt32Array) == path:
			return i
	return -1


func sync_selection() -> void:
	if _list == null or main == null or main.doc == null:
		return
	# select() 会回抛 item_selected，那会把多选又缩回单选 —— 加个闸门。
	_syncing = true
	_list.deselect_all()
	for p in main.selected_paths():
		var row := _row_of(p)
		if row >= 0:
			_list.select(row, false)
	_syncing = false
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


## 点容器行最左边的箭头 = 折叠 / 展开（不改选中）。
## 判定只看 x：ItemList 没有"点在第几列"的概念，箭头固定在行首那一小块。
func _on_list_input(event: InputEvent) -> void:
	if not (event is InputEventMouseButton):
		return
	var mb := event as InputEventMouseButton
	if not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT:
		return
	var row := _list.get_item_at_position(mb.position, true)
	if row < 0 or row >= _rows.size():
		return
	if mb.position.x > 22.0:
		return
	var entry: Dictionary = _rows[row]
	var layer: Dictionary = entry["layer"]
	if not Layer.is_group(layer):
		return
	var id := String(layer.get("id", ""))
	if id.is_empty():
		return
	_folded[id] = not bool(_folded.get(id, false))
	get_viewport().set_input_as_handled()
	refresh()


func _is_folded(layer: Dictionary) -> bool:
	return bool(_folded.get(String(layer.get("id", "")), false))


func _on_item_selected(row: int) -> void:
	if _syncing:
		return
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
		5:
			_add_effect(Layer.make_mask("遮罩层"))
		6:
			_add_effect(Layer.make_filter("滤镜层"))


## 选中容器时把新层加进容器，否则加到根的最上层。
func _add_layer(layer: Dictionary) -> void:
	var parent := PackedInt32Array()
	var current: Dictionary = main.selected_layer()
	if not current.is_empty() and Layer.is_group(current):
		parent = main.sel.duplicate()
	var path: PackedInt32Array = main.doc.insert_layer(parent, layer)
	main.select_layer_path(path)
	main.apply_change(true)


## 效果层紧贴在当前图层**上面**插入，并**绑定**到刚选中的那一层（只作用在它身上）。
## 绑定与位置无关：目标以后怎么上下移动、中间插进多少图层，效果范围都不变。
## 选中的是容器时，效果层加进容器里、覆盖容器里的全部内容 —— 一个滤镜管一组元素。
## 没选中任何图层时加到最上层、覆盖下方全部。
func _add_effect(layer: Dictionary) -> void:
	var parent := PackedInt32Array()
	var index := -1
	var cover := {"mode": "all", "count": 1}
	if not main.sel.is_empty():
		var current: Dictionary = main.selected_layer()
		if not current.is_empty() and Layer.is_group(current):
			parent = main.sel.duplicate()
		elif not current.is_empty():
			parent = main.doc.parent_path_of(main.sel)
			index = main.sel[main.sel.size() - 1] + 1
			cover = {"mode": "layer", "target": String(current.get("id", ""))}
	layer["cover"] = cover
	var path: PackedInt32Array = main.doc.insert_layer(parent, layer, index)
	main.select_layer_path(path)
	main.apply_change(true)


func _on_delete() -> void:
	# 多选时全部删掉（main 里按深度/下标从大到小删，不会串位）。
	main.delete_selection()


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
		_file_dialog.title = "选择图片或 GIF（会拷进工程 assets/）"
		_file_dialog.filters = AssetImport.image_filters(true)
		_file_dialog.file_selected.connect(_on_image_selected)
		add_child(_file_dialog)
	_file_dialog.current_dir = dir
	_file_dialog.popup_centered_ratio(0.7)


func _on_image_selected(path: String) -> void:
	# GIF 直接当动图：拆成 PNG 帧后建图层，省得"先加静态图、再去属性面板导入动图"。
	if path.get_extension().to_lower() == "gif":
		var res: Dictionary = AssetImport.import_gif(main.ensure_project_dir(), path)
		if not res.ok:
			main.show_error("添加 GIF 失败", String(res.error))
			return
		var layer := Layer.make_image(path.get_file().get_basename(), String(res.frames[0]))
		layer["frames"] = res.frames
		layer["fps"] = float(res.fps)
		_add_layer(layer)
		return
	var rel := AssetImport.import_image(main.ensure_project_dir(), path)
	if rel.is_empty():
		main.show_error("添加图片失败", "无法把图片拷进工程：\n" + path)
		return
	_add_layer(Layer.make_image(path.get_file().get_basename(), rel))
