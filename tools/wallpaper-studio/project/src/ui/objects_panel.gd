extends Control
## 3D 对象列表面板（mode = "3d" 时顶替图层列表）。
##
## 3D 场景没有图层树：列表就是 objects3d（index 0 = 先建的场景根成员，最上面一行
## 恒是「相机」，它不是物体、不能删，但属性要在 Inspector3D 里编辑）。
## 选择沿用 main.sel 的路径模型：相机 = [-1]，物体 = [index]。

const Scene3D = preload("res://src/doc/scene3d.gd")

const CAMERA_PATH := -1          # sel 里用这个下标表示"选中相机"

const TYPE_NAMES := {
	"box": "立方体", "sphere": "球", "cylinder": "圆柱", "torus": "环面",
	"plane": "地面", "text": "3D 文字", "light_sun": "平行光", "light_omni": "点光",
	"model": "模型", "camera": "相机",
}
## 「＋ 添加」菜单：标签 -> Scene3D 里的 make_*。
const ADD_ITEMS := [
	["立方体", "box"], ["球", "sphere"], ["圆柱", "cylinder"], ["环面", "torus"],
	["地面", "plane"], ["3D 文字", "text"], ["平行光", "light_sun"],
	["点光", "light_omni"], ["模型（.glb）", "model"],
]

var main = null
var _list: ItemList
var _add_menu: MenuButton
var _btn_delete: Button
var _btn_copy: Button
var _btn_visible: Button
var _file_dialog: FileDialog


func _ready() -> void:
	var box := VBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	box.add_theme_constant_override("separation", 4)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 8)
	margin.add_theme_constant_override("margin_right", 8)
	margin.add_theme_constant_override("margin_top", 6)
	margin.add_theme_constant_override("margin_bottom", 6)
	add_child(margin)
	margin.add_child(box)

	var head := HBoxContainer.new()
	box.add_child(head)
	var title := Label.new()
	title.text = "3D 对象"
	title.add_theme_font_size_override("font_size", 13)
	head.add_child(title)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(spacer)
	_add_menu = MenuButton.new()
	_add_menu.text = "＋ 添加"
	_add_menu.focus_mode = Control.FOCUS_NONE
	_add_menu.add_theme_font_size_override("font_size", 13)
	head.add_child(_add_menu)
	var popup := _add_menu.get_popup()
	for item in ADD_ITEMS:
		popup.add_item(String(item[0]))
	popup.id_pressed.connect(_on_add)

	_list = ItemList.new()
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.custom_minimum_size = Vector2(0, 120)
	_list.focus_mode = Control.FOCUS_CLICK
	_list.add_theme_font_size_override("font_size", 13)
	_list.select_mode = ItemList.SELECT_SINGLE
	box.add_child(_list)
	_list.item_selected.connect(_on_selected)

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 4)
	box.add_child(buttons)
	_btn_delete = _small_button(buttons, "删除")
	_btn_copy = _small_button(buttons, "复制")
	_btn_visible = _small_button(buttons, "显示")
	_btn_delete.pressed.connect(_on_delete)
	_btn_copy.pressed.connect(_on_copy)
	_btn_visible.pressed.connect(_on_toggle_visible)
	_update_buttons()

	_file_dialog = FileDialog.new()
	_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_file_dialog.filters = PackedStringArray(["*.glb ; glTF 二进制", "*.gltf ; glTF", "*.obj ; Wavefront OBJ"])
	_file_dialog.title = "导入 3D 模型"
	add_child(_file_dialog)
	_file_dialog.file_selected.connect(_on_model_picked)


func setup(m) -> void:
	main = m


func _small_button(parent: Control, text: String) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 12)
	parent.add_child(b)
	return b


# ---------------------------------------------------------------- 列表

func refresh() -> void:
	if main == null or main.doc == null or _list == null:
		return
	_list.clear()
	_list.add_item("相机    ·    视角 / 路径")
	for o in main.doc.objects3d:
		if not (o is Dictionary):
			continue
		var type := String(o.get("type", "box"))
		var label := "%s    ·    %s" % [String(o.get("name", "物体")),
				String(TYPE_NAMES.get(type, type))]
		if not bool(o.get("visible", true)):
			label = "（隐藏）" + label
		var idx := _list.add_item(label)
		if not bool(o.get("visible", true)):
			_list.set_item_custom_fg_color(idx, Color(0.52, 0.54, 0.6))
		elif Scene3D.is_light(o):
			_list.set_item_custom_fg_color(idx, Color(0.96, 0.86, 0.55))
	sync_selection()
	_update_buttons()


## main.sel -> 列表高亮。列表行 = 相机（行 0）+ 物体（行 index + 1）。
func sync_selection() -> void:
	if _list == null or main == null:
		return
	var row := -1
	if main.sel.size() == 1:
		if int(main.sel[0]) == CAMERA_PATH:
			row = 0
		else:
			row = int(main.sel[0]) + 1
	for i in _list.item_count:
		_list.set_selected(i, i == row)


func _on_selected(index: int) -> void:
	if main == null:
		return
	if index == 0:
		main.select_layer_path(PackedInt32Array([CAMERA_PATH]))
	else:
		main.select_layer_path(PackedInt32Array([index - 1]))


func _update_buttons() -> void:
	var has_sel: bool = main != null and main.sel.size() == 1 and int(main.sel[0]) != CAMERA_PATH
	_btn_delete.disabled = not has_sel
	_btn_copy.disabled = not has_sel
	_btn_visible.disabled = main == null or main.sel.is_empty()
	_btn_visible.text = "显示" if _sel_visible() else "隐藏"


func _sel_obj() -> Dictionary:
	if main == null or main.doc == null or main.sel.size() != 1:
		return {}
	var i := int(main.sel[0])
	if i < 0 or i >= main.doc.objects3d.size():
		return {}
	return main.doc.objects3d[i]


func _sel_visible() -> bool:
	var o := _sel_obj()
	return bool(o.get("visible", true)) if not o.is_empty() else true


# ---------------------------------------------------------------- 操作

func _on_add(kind: int) -> void:
	if main == null or main.doc == null:
		return
	var type := String(ADD_ITEMS[clampi(kind, 0, ADD_ITEMS.size() - 1)][1])
	if type == "model":
		_file_dialog.popup_centered(Vector2i(720, 480))
		return
	var made: Dictionary = _make_typed(type, "物体 %d" % (main.doc.objects3d.size() + 1))
	main.doc.objects3d.append(made)
	main.select_layer_path(PackedInt32Array([main.doc.objects3d.size() - 1]))
	main.apply_change(true, "添加 %s" % String(TYPE_NAMES.get(type, type)))


static func _make_typed(type: String, obj_name: String) -> Dictionary:
	match type:
		"sphere":
			return Scene3D.make_sphere(obj_name)
		"cylinder":
			return Scene3D.make_cylinder(obj_name)
		"torus":
			return Scene3D.make_torus(obj_name)
		"plane":
			return Scene3D.make_plane(obj_name)
		"text":
			return Scene3D.make_text(obj_name)
		"light_sun":
			return Scene3D.make_light_sun(obj_name)
		"light_omni":
			return Scene3D.make_light_omni(obj_name)
		_:
			return Scene3D.make_box(obj_name)


func _on_model_picked(path: String) -> void:
	if main == null or main.doc == null:
		return
	var dir: String = main.ensure_project_dir()
	if dir.is_empty():
		return
	# 模型拷进工程 assets/（和图片导入同一套约定），文档里存相对路径。
	var file_name := path.get_file()
	var dst: String = dir.path_join("assets").path_join(file_name)
	DirAccess.make_dir_recursive_absolute(dir.path_join("assets"))
	if not FileAccess.file_exists(dst):
		var bytes := FileAccess.get_file_as_bytes(path)
		var f := FileAccess.open(dst, FileAccess.WRITE)
		if f == null:
			main.show_error("导入失败", "无法写入：" + dst)
			return
		f.store_buffer(bytes)
		f.close()
	var made: Dictionary = Scene3D.make_model(file_name.get_basename(), "assets/" + file_name)
	main.doc.objects3d.append(made)
	main.select_layer_path(PackedInt32Array([main.doc.objects3d.size() - 1]))
	main.apply_change(true, "导入模型")


func _on_delete() -> void:
	var i := _sel_index()
	if i < 0 or main == null:
		return
	main.doc.objects3d.remove_at(i)
	main.select_layer_path(PackedInt32Array())
	main.apply_change(true, "删除对象")


func _on_copy() -> void:
	var i := _sel_index()
	if i < 0 or main == null or main.doc == null:
		return
	var copy: Dictionary = (main.doc.objects3d[i] as Dictionary).duplicate(true)
	copy["id"] = "o_" + _new_id()
	copy["name"] = String(copy.get("name", "物体")) + " 副本"
	copy["pos"] = [float(copy["pos"][0]) + 1.0, float(copy["pos"][1]),
			float(copy["pos"][2])]
	main.doc.objects3d.insert(i + 1, copy)
	main.select_layer_path(PackedInt32Array([i + 1]))
	main.apply_change(true, "复制对象")


static func _new_id() -> String:
	return "%08x" % (randi() & 0xFFFFFFFF)


func _on_toggle_visible() -> void:
	var o := _sel_obj()
	if o.is_empty() or main == null:
		return
	o["visible"] = not bool(o.get("visible", true))
	main.apply_change(true, "显示/隐藏")


func _sel_index() -> int:
	if main == null or main.sel.size() != 1:
		return -1
	var i := int(main.sel[0])
	if i < 0 or main.doc == null or i >= main.doc.objects3d.size():
		return -1
	return i
