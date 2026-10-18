extends ScrollContainer
## 属性面板：没选中图层时显示工程信息，选中后按图层类型动态建设控件。
##
## 重建策略：只有当「选中的图层 / 类型 / id」变了才重建整面板。控件内部改值只写回文档
## 并重建画布 —— 不重建自己，否则拖滑块时控件会被换掉，手感直接坏掉。

const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const AssetImport = preload("res://src/ui/asset_import.gd")

const TYPE_NAMES := {
	"color": "纯色",
	"image": "图片",
	"text": "文字",
	"particles": "粒子",
}
const BLEND_KEYS := ["mix", "add", "sub", "mul", "screen", "overlay"]
const BLEND_NAMES := ["正常", "相加", "相减", "正片叠底", "滤色", "叠加"]
const FIT_KEYS := ["fill", "contain", "cover", "tile"]
const FIT_NAMES := ["拉伸填满", "等比适应", "等比裁切", "平铺"]
const ALIGN_KEYS := ["left", "center", "right"]
const ALIGN_NAMES := ["左对齐", "居中", "右对齐"]
const DYNAMIC_KEYS := ["none", "clock", "date"]
const DYNAMIC_NAMES := ["静态文本", "时钟", "日期"]

var main = null

var _box: VBoxContainer
var _sig := ""
var _file_dialog: FileDialog
var _pick_layer: Dictionary = {}
var _pick_field := ""


func setup(m) -> void:
	main = m


func _ready() -> void:
	horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	# 属性面板要有自己的底色：ScrollContainer 默认不画背景，露出的是窗口底色。
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.105, 0.11, 0.125)
	sb.border_width_left = 1
	sb.border_color = Color(0.2, 0.21, 0.245)
	add_theme_stylebox_override("panel", sb)

	var margin := MarginContainer.new()
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	margin.add_theme_constant_override("margin_left", 12)
	margin.add_theme_constant_override("margin_right", 12)
	margin.add_theme_constant_override("margin_top", 10)
	margin.add_theme_constant_override("margin_bottom", 12)
	add_child(margin)

	_box = VBoxContainer.new()
	_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_box.add_theme_constant_override("separation", 6)
	margin.add_child(_box)


func refresh(force: bool = false) -> void:
	if main == null or main.doc == null or _box == null:
		return
	var sig := _signature()
	if not force and sig == _sig:
		return
	_sig = sig
	_rebuild()


func _signature() -> String:
	if main.selected < 0 or main.selected >= main.doc.layers.size():
		return "doc"
	var l: Dictionary = main.doc.layers[main.selected]
	return "layer:%d:%s:%s" % [main.selected, String(l.get("type", "")), String(l.get("id", ""))]


func _rebuild() -> void:
	for c in _box.get_children():
		_box.remove_child(c)
		c.free()
	if main.selected < 0 or main.selected >= main.doc.layers.size():
		_build_doc()
	else:
		_build_layer(main.doc.layers[main.selected])


# ---------------------------------------------------------------- 工程信息

func _build_doc() -> void:
	_header("工程")
	_section("文档")
	var name_edit := _line(String(main.doc.name))
	name_edit.text_changed.connect(func(t: String) -> void:
		main.doc.name = t
		main.mark_dirty()
	)
	_row("名称", name_edit)

	var size_label := Label.new()
	size_label.text = "%d × %d" % [main.doc.canvas.x, main.doc.canvas.y]
	_row("画布", size_label)

	var bg := _color(main.doc.background)
	bg.color_changed.connect(func(c: Color) -> void:
		main.doc.background = c
		main.apply_change()
	)
	_row("背景", bg)

	_section("工程目录")
	var dir_label := Label.new()
	dir_label.text = main.project_dir if not main.project_dir.is_empty() else "（保存时自动创建）"
	dir_label.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	dir_label.modulate = Color(0.58, 0.61, 0.68)
	dir_label.add_theme_font_size_override("font_size", 11)
	_box.add_child(dir_label)

	_hint("在左侧选择一个图层来编辑它的属性。")
	_hint("画布按「纯色 → 图片 → 文字 → 粒子」的顺序自下而上叠加；列表顶部是最上层。")


# ---------------------------------------------------------------- 图层属性

func _build_layer(layer: Dictionary) -> void:
	var type_name := String(TYPE_NAMES.get(String(layer.get("type", "color")), "图层"))
	_header("属性 · " + type_name)

	_section("通用")
	var name_edit := _line(String(layer.get("name", "")))
	name_edit.text_changed.connect(func(t: String) -> void:
		layer["name"] = t
		main.refresh_layers()
	)
	_row("名称", name_edit)

	var visible := CheckButton.new()
	visible.text = "在画布上显示"
	visible.button_pressed = bool(layer.get("visible", true))
	visible.toggled.connect(func(on: bool) -> void:
		layer["visible"] = on
		main.apply_change()
	)
	_box.add_child(visible)

	_transform_row("X", "x", layer, 0.0, -20000.0, 20000.0, 1.0)
	_transform_row("Y", "y", layer, 0.0, -20000.0, 20000.0, 1.0)
	_size_row("宽", "w", layer)
	_size_row("高", "h", layer)
	_transform_row("旋转", "rotation", layer, 0.0, -3600.0, 3600.0, 1.0)
	_hint("画布上直接拖动元素即可移动；拖八个手柄缩放尺寸，角点按住 Shift 等比。宽高和图片比例不一致时图片会被拉伸。")

	var opacity := _spin(float(layer["transform"].get("opacity", 1.0)), 0.0, 1.0, 0.01)
	opacity.value_changed.connect(func(v: float) -> void:
		layer["transform"]["opacity"] = v
		main.apply_change()
	)
	_row("不透明度", opacity)

	var blend := _option(BLEND_KEYS, BLEND_NAMES, String(layer.get("blend", "mix")))
	blend.item_selected.connect(func(i: int) -> void:
		layer["blend"] = BLEND_KEYS[i]
		main.apply_change()
	)
	_row("混合模式", blend)

	match String(layer.get("type", "color")):
		"image":
			_build_image(layer)
		"text":
			_build_text(layer)
		"particles":
			_build_particles(layer)
		_:
			_build_color(layer)


func _build_color(layer: Dictionary) -> void:
	_section("颜色")
	var c := _color(Layer.to_color(layer.get("color"), Color.WHITE))
	c.color_changed.connect(func(col: Color) -> void:
		layer["color"] = col
		main.apply_change()
	)
	_row("填充", c)


func _build_image(layer: Dictionary) -> void:
	_section("图片")
	var src := String(layer.get("src", ""))
	var path_label := Label.new()
	path_label.text = src if not src.is_empty() else "（未选择）"
	path_label.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	path_label.modulate = Color(0.7, 0.73, 0.8)
	path_label.add_theme_font_size_override("font_size", 11)
	_row("文件", path_label)

	var pick := Button.new()
	pick.text = "选择图片…"
	pick.pressed.connect(func() -> void: _pick_texture(layer, "src"))
	_row("替换", pick)

	var fit := _option(FIT_KEYS, FIT_NAMES, String(layer.get("fit", "contain")))
	fit.item_selected.connect(func(i: int) -> void:
		layer["fit"] = FIT_KEYS[i]
		main.apply_change()
	)
	_row("填充方式", fit)


func _build_text(layer: Dictionary) -> void:
	_section("文字")
	var dynamic_value := String(layer.get("dynamic", "none"))
	var edit := TextEdit.new()
	edit.text = String(layer.get("text", ""))
	edit.custom_minimum_size = Vector2(0, 72)
	edit.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	# 时钟/日期模式下这段文字不参与渲染，置灰避免"改了没反应"的错觉。
	edit.editable = dynamic_value == "none"
	edit.text_changed.connect(func() -> void:
		layer["text"] = edit.text
		main.apply_change()
	)
	_box.add_child(edit)

	var font_size := _spin(float(layer.get("font_size", 64)), 4.0, 600.0, 1.0)
	font_size.value_changed.connect(func(v: float) -> void:
		layer["font_size"] = int(v)
		main.apply_change()
	)
	_row("字号", font_size)

	var c := _color(Layer.to_color(layer.get("color"), Color.WHITE))
	c.color_changed.connect(func(col: Color) -> void:
		layer["color"] = col
		main.apply_change()
	)
	_row("颜色", c)

	var align := _option(ALIGN_KEYS, ALIGN_NAMES, String(layer.get("align", "center")))
	align.item_selected.connect(func(i: int) -> void:
		layer["align"] = ALIGN_KEYS[i]
		main.apply_change()
	)
	_row("对齐", align)

	var dynamic := _option(DYNAMIC_KEYS, DYNAMIC_NAMES, dynamic_value)
	dynamic.item_selected.connect(func(i: int) -> void:
		layer["dynamic"] = DYNAMIC_KEYS[i]
		edit.editable = DYNAMIC_KEYS[i] == "none"
		main.apply_change()
	)
	_row("内容", dynamic)

	var fmt := _line(String(layer.get("date_format", "%04d-%02d-%02d")))
	fmt.text_changed.connect(func(t: String) -> void:
		layer["date_format"] = t
		main.apply_change()
	)
	_row("日期格式", fmt)
	_hint("「时钟 / 日期」模式下文本框不参与导出，显示内容由「日期格式」决定（C 风格：%04d-%02d-%02d）。")


func _build_particles(layer: Dictionary) -> void:
	_section("粒子")
	var tex := String(layer.get("texture", ""))
	var tex_label := Label.new()
	tex_label.text = tex if not tex.is_empty() else "（内置圆点）"
	tex_label.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	tex_label.modulate = Color(0.7, 0.73, 0.8)
	tex_label.add_theme_font_size_override("font_size", 11)
	_row("贴图", tex_label)

	var pick := Button.new()
	pick.text = "选择贴图…"
	pick.pressed.connect(func() -> void: _pick_texture(layer, "texture"))
	_row("替换", pick)

	var amount := _spin(float(layer.get("amount", 120)), 1.0, 4000.0, 1.0)
	amount.value_changed.connect(func(v: float) -> void:
		layer["amount"] = int(v)
		main.apply_change()
	)
	_row("数量", amount)

	var lifetime := _spin(float(layer.get("lifetime", 6.0)), 0.1, 120.0, 0.1)
	lifetime.value_changed.connect(func(v: float) -> void:
		layer["lifetime"] = v
		main.apply_change()
	)
	_row("生命（秒）", lifetime)

	var velocity := _spin(float(layer.get("velocity", 40.0)), 0.0, 5000.0, 1.0)
	velocity.value_changed.connect(func(v: float) -> void:
		layer["velocity"] = v
		main.apply_change()
	)
	_row("速度", velocity)

	var spread := _spin(float(layer.get("spread", 45.0)), 0.0, 360.0, 1.0)
	spread.value_changed.connect(func(v: float) -> void:
		layer["spread"] = v
		main.apply_change()
	)
	_row("扩散角", spread)

	_vec_row("方向", "direction", layer, {"x": 0.0, "y": -1.0})
	_vec_row("重力", "gravity", layer, {"x": 0.0, "y": 20.0})

	var scale_min := _spin(float(layer.get("scale_min", 0.4)), 0.01, 10.0, 0.01)
	scale_min.value_changed.connect(func(v: float) -> void:
		layer["scale_min"] = v
		main.apply_change()
	)
	_row("最小尺寸", scale_min)

	var scale_max := _spin(float(layer.get("scale_max", 1.0)), 0.01, 10.0, 0.01)
	scale_max.value_changed.connect(func(v: float) -> void:
		layer["scale_max"] = v
		main.apply_change()
	)
	_row("最大尺寸", scale_max)

	_hint("粒子贴图留空时使用内置白色圆点，导出时会内联进场景。")


# ---------------------------------------------------------------- 图片导入

func _pick_texture(layer: Dictionary, field: String) -> void:
	var dir: String = main.ensure_project_dir()
	if dir.is_empty():
		main.show_error("无法导入图片", "工程目录不可写。")
		return
	_pick_layer = layer
	_pick_field = field
	if _file_dialog == null:
		_file_dialog = FileDialog.new()
		_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_file_dialog.title = "选择图片（会拷进工程 assets/）"
		_file_dialog.filters = AssetImport.image_filters()
		_file_dialog.file_selected.connect(_on_texture_selected)
		add_child(_file_dialog)
	_file_dialog.current_dir = dir
	_file_dialog.popup_centered_ratio(0.7)


func _on_texture_selected(path: String) -> void:
	if _pick_layer.is_empty() or _pick_field.is_empty():
		return
	var rel := AssetImport.import_image(main.ensure_project_dir(), path)
	if rel.is_empty():
		main.show_error("图片导入失败", "无法把图片拷进工程：\n" + path)
		return
	_pick_layer[_pick_field] = rel
	_pick_layer = {}
	_pick_field = ""
	# 路径文字要跟着变，所以强制重建属性面板。
	main.apply_change(true)


# ---------------------------------------------------------------- 小控件

func _header(text: String) -> void:
	var lab := Label.new()
	lab.text = text
	lab.add_theme_font_size_override("font_size", 15)
	lab.modulate = Color(0.9, 0.92, 0.97)
	_box.add_child(lab)


func _section(text: String) -> void:
	var sep := HSeparator.new()
	_box.add_child(sep)
	var lab := Label.new()
	lab.text = text
	lab.add_theme_font_size_override("font_size", 12)
	lab.modulate = Color(0.6, 0.65, 0.74)
	_box.add_child(lab)


func _hint(text: String) -> void:
	var lab := Label.new()
	lab.text = text
	lab.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	lab.modulate = Color(0.55, 0.58, 0.65)
	lab.add_theme_font_size_override("font_size", 11)
	_box.add_child(lab)


func _row(label_text: String, control: Control) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	var lab := Label.new()
	lab.text = label_text
	lab.custom_minimum_size = Vector2(76, 0)
	lab.add_theme_font_size_override("font_size", 12)
	lab.modulate = Color(0.72, 0.75, 0.82)
	row.add_child(lab)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)
	_box.add_child(row)


func _line(text: String) -> LineEdit:
	var e := LineEdit.new()
	e.text = text
	return e


func _spin(value: float, minv: float, maxv: float, step: float) -> SpinBox:
	var s := SpinBox.new()
	s.min_value = minv
	s.max_value = maxv
	s.step = step
	s.value = value
	s.custom_minimum_size = Vector2(88, 0)
	return s


func _color(c: Color) -> ColorPickerButton:
	var b := ColorPickerButton.new()
	b.color = c
	b.custom_minimum_size = Vector2(64, 24)
	b.edit_alpha = false
	return b


func _option(keys: Array, names: Array, current: String) -> OptionButton:
	var o := OptionButton.new()
	for i in keys.size():
		o.add_item(String(names[i]), i)
	var idx := keys.find(current)
	o.select(maxi(idx, 0))
	return o


func _transform_row(label_text: String, key: String, layer: Dictionary,
		default_value: float, minv: float, maxv: float, step: float) -> void:
	var tr: Dictionary = layer["transform"]
	if not tr.has(key):
		tr[key] = default_value
	var sp := _spin(float(tr.get(key, default_value)), minv, maxv, step)
	sp.value_changed.connect(func(v: float) -> void:
		tr[key] = v
		main.apply_change()
	)
	_row(label_text, sp)


## 宽/高。没定过尺寸的图层显示的是画布尺寸；第一次改值时才把框落成显式数字
## （Frame.materialize），这样打开的旧文档不会被无声改写。
func _size_row(label_text: String, key: String, layer: Dictionary) -> void:
	var r := Frame.of(layer, main.doc.canvas)
	var sp := _spin(r.size.x if key == "w" else r.size.y, Frame.MIN_SIZE, 20000.0, 1.0)
	sp.value_changed.connect(func(v: float) -> void:
		Frame.materialize(layer, main.doc.canvas)
		layer["transform"][key] = maxf(Frame.MIN_SIZE, v)
		main.apply_change()
	)
	_row(label_text, sp)


func _vec_row(label_text: String, key: String, layer: Dictionary, fallback: Dictionary) -> void:
	var vec: Dictionary = layer.get(key, {})
	if vec.is_empty():
		vec = fallback.duplicate()
		layer[key] = vec
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 4)
	var sx := _spin(float(vec.get("x", 0.0)), -5000.0, 5000.0, 1.0)
	sx.tooltip_text = "X"
	sx.value_changed.connect(func(v: float) -> void:
		vec["x"] = v
		main.apply_change()
	)
	var sy := _spin(float(vec.get("y", 0.0)), -5000.0, 5000.0, 1.0)
	sy.tooltip_text = "Y"
	sy.value_changed.connect(func(v: float) -> void:
		vec["y"] = v
		main.apply_change()
	)
	row.add_child(sx)
	row.add_child(sy)
	_row(label_text, row)
