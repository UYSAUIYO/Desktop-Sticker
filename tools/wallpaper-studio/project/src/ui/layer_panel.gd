extends PanelContainer
## 图层面板：列表顶部 = 最上层（与画布叠放顺序一致），下标换算只在这里做一次。
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
}

var main = null

var _list: ItemList
var _empty_hint: Label
var _add_menu: MenuButton
var _btn_delete: Button
var _btn_up: Button
var _btn_down: Button
var _btn_visible: Button
var _file_dialog: FileDialog


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
	_add_menu.tooltip_text = "添加图层"
	var popup := _add_menu.get_popup()
	popup.add_item("纯色背景", 0)
	popup.add_item("图片", 1)
	popup.add_item("文字", 2)
	popup.add_item("粒子", 3)
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

	_btn_delete = _small_button(buttons, "删除", "删除选中图层")
	_btn_up = _small_button(buttons, "上移", "向上（更靠前）移动一层")
	_btn_down = _small_button(buttons, "下移", "向下（更靠后）移动一层")
	_btn_visible = _small_button(buttons, "显示", "显示 / 隐藏选中图层")

	_btn_delete.pressed.connect(_on_delete)
	_btn_up.pressed.connect(_on_move.bind(1))
	_btn_down.pressed.connect(_on_move.bind(-1))
	_btn_visible.pressed.connect(_on_toggle_visible)

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

## 列表行 -> 文档下标。顶部 = 最上层 = 文档最后一个元素。
func _row_to_doc(row: int) -> int:
	if main == null or main.doc == null:
		return -1
	return main.doc.layers.size() - 1 - row


func _doc_to_row(index: int) -> int:
	if main == null or main.doc == null:
		return -1
	return main.doc.layers.size() - 1 - index


func refresh() -> void:
	if main == null or main.doc == null or _list == null:
		return
	_list.clear()
	var count: int = main.doc.layers.size()
	for i in range(count - 1, -1, -1):
		var l: Dictionary = main.doc.layers[i]
		var type_name := String(TYPE_NAMES.get(String(l.get("type", "color")), "图层"))
		var label := "%s    ·    %s" % [String(l.get("name", "图层")), type_name]
		if not bool(l.get("visible", true)):
			label = "（隐藏）" + label
		var row := _list.add_item(label)
		if not bool(l.get("visible", true)):
			_list.set_item_custom_fg_color(row, Color(0.52, 0.54, 0.6))
	if count > 0:
		if main.selected < 0 or main.selected >= count:
			# 默认选中最上层：打开工程后属性面板立刻有内容，不用先点一下。
			main.select_layer(count - 1)
		_list.select(_doc_to_row(main.selected))
	_empty_hint.visible = count == 0
	_update_buttons()


func _update_buttons() -> void:
	if _list == null:
		return
	var has: bool = (main != null and main.doc != null and main.selected >= 0
			and main.selected < main.doc.layers.size())
	_btn_delete.disabled = not has
	_btn_up.disabled = not has or main.selected >= main.doc.layers.size() - 1
	_btn_down.disabled = not has or main.selected <= 0
	_btn_visible.disabled = not has
	if has:
		var l: Dictionary = main.doc.layers[main.selected]
		_btn_visible.text = "隐藏" if bool(l.get("visible", true)) else "显示"


func _on_item_selected(row: int) -> void:
	var index := _row_to_doc(row)
	if index >= 0:
		main.select_layer(index)


## 只把列表选中行与按钮状态同步到当前选中图层，不重建条目。
## main.select_layer() 是程序化改选中的唯一入口（新建/删除图层也会走它），
## 不同步的话列表还高亮着旧行，属性面板却已经是另一个图层。
func sync_selection() -> void:
	if _list == null or main == null or main.doc == null:
		return
	if main.selected >= 0 and main.selected < main.doc.layers.size():
		_list.select(_doc_to_row(main.selected))
	else:
		_list.deselect_all()
	_update_buttons()


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


func _add_layer(layer: Dictionary) -> void:
	main.doc.add_layer(layer)
	main.selected = main.doc.layers.size() - 1
	main.apply_change(true)


func _on_delete() -> void:
	if main == null or main.doc == null or main.selected < 0:
		return
	main.doc.remove_layer(main.selected)
	if main.doc.layers.is_empty():
		main.selected = -1
	else:
		main.selected = clampi(main.selected, 0, main.doc.layers.size() - 1)
	main.apply_change(true)


func _on_move(delta: int) -> void:
	if main == null or main.doc == null or main.selected < 0:
		return
	var to: int = main.selected + delta
	if to < 0 or to >= main.doc.layers.size():
		return
	main.doc.move_layer(main.selected, to)
	main.selected = to
	main.apply_change(true)


func _on_toggle_visible() -> void:
	if main == null or main.doc == null or main.selected < 0:
		return
	var l: Dictionary = main.doc.layers[main.selected]
	l["visible"] = not bool(l.get("visible", true))
	main.apply_change(false)


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
