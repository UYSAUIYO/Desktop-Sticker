extends ScrollContainer
## 属性面板：没选中图层时显示工程信息（含画布设置），选中后按图层类型动态建设控件。
##
## 重建策略：只有当「选中的图层 / 类型 / id」变了才重建整面板。控件内部改值只写回文档
## 并重建画布 —— 不重建自己，否则拖滑块时控件会被换掉，手感直接坏掉。
##
## 面板分段：通用 → （内容图层：类型专属 + 容器）／（效果图层：覆盖范围 + 遮罩/滤镜）。
## 遮罩和滤镜是**独立图层**，所以它们不在普通图层的面板里 —— 选中遮罩层/滤镜层才有。

const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")
const AssetImport = preload("res://src/ui/asset_import.gd")

const TYPE_NAMES := {
	"color": "纯色",
	"image": "图片",
	"text": "文字",
	"particles": "粒子",
	"group": "容器",
	"mask": "遮罩层",
	"filter": "滤镜层",
}
const BLEND_KEYS := ["mix", "add", "sub", "mul", "screen", "overlay"]
const BLEND_NAMES := ["正常", "相加", "相减", "正片叠底", "滤色", "叠加"]
const FIT_KEYS := ["fill", "contain", "cover", "tile"]
const FIT_NAMES := ["拉伸填满", "等比适应", "等比裁切", "平铺"]
const ALIGN_KEYS := ["left", "center", "right"]
const ALIGN_NAMES := ["左对齐", "居中", "右对齐"]
const DYNAMIC_KEYS := ["none", "clock", "date"]
const DYNAMIC_NAMES := ["静态文本", "时钟", "日期"]
const SHAPE_NAMES := ["矩形", "圆角矩形", "椭圆", "图片 alpha"]
const COVER_MODES := ["all", "count", "range"]
const COVER_MODE_NAMES := ["下方全部图层", "下方紧邻 N 层", "指定区间（第几层到第几层）"]
const PRESETS := [
	{"name": "1920 × 1080（1080p）", "size": Vector2i(1920, 1080)},
	{"name": "2560 × 1440（2K）", "size": Vector2i(2560, 1440)},
	{"name": "3840 × 2160（4K）", "size": Vector2i(3840, 2160)},
	{"name": "1080 × 1920（竖屏）", "size": Vector2i(1080, 1920)},
	{"name": "1080 × 1080（方形）", "size": Vector2i(1080, 1080)},
]

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
	var layer: Dictionary = main.selected_layer()
	if layer.is_empty():
		return "doc"
	var parts := PackedStringArray()
	for i in main.sel.size():
		parts.append(str(main.sel[i]))
	return "layer:%s:%s:%s" % [",".join(parts), String(layer.get("type", "")),
			String(layer.get("id", ""))]


func _rebuild() -> void:
	# 必须 remove_child + queue_free，不能 free()：这些控件自己是信号的发射方
	# （OptionButton.item_selected / SpinBox.value_changed 里就会重进到这儿），
	# 在信号发射途中把发射方释放掉，引擎会报
	# "Object was freed or unreferenced while a signal is being emitted from it"。
	for c in _box.get_children():
		_box.remove_child(c)
		c.queue_free()
	var layer: Dictionary = main.selected_layer()
	if layer.is_empty():
		_build_doc()
	else:
		_build_layer(layer)


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

	_section("画布")
	var w := _spin(float(main.doc.canvas.x), 64.0, 16384.0, 1.0)
	var h := _spin(float(main.doc.canvas.y), 64.0, 16384.0, 1.0)
	_row("宽", w)
	_row("高", h)

	var preset := OptionButton.new()
	preset.add_item("选择预设…", -1)
	for i in PRESETS.size():
		preset.add_item(String(PRESETS[i]["name"]), i)
	preset.item_selected.connect(func(i: int) -> void:
		if i < 0:
			return
		var size: Vector2i = PRESETS[i]["size"]
		w.value = float(size.x)
		h.value = float(size.y)
		_apply_canvas(Vector2i(size.x, size.y))
	)
	_row("预设", preset)

	var apply := Button.new()
	apply.text = "应用画布尺寸（按比例缩放元素）"
	apply.pressed.connect(func() -> void:
		_apply_canvas(Vector2i(int(w.value), int(h.value)))
	)
	_box.add_child(apply)

	var bg := _color(main.doc.background)
	bg.color_changed.connect(func(c: Color) -> void:
		main.doc.background = c
		main.apply_change()
	)
	_row("背景", bg)
	_hint("改画布尺寸默认把现有元素按比例拉伸（换 4K 不用重新摆一遍）；"
			+ "想保持像素尺寸就把元素先选中再逐个改宽高。")

	_section("工程目录")
	var dir_label := Label.new()
	dir_label.text = main.project_dir if not main.project_dir.is_empty() else "（保存时自动创建）"
	dir_label.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	dir_label.modulate = Color(0.58, 0.61, 0.68)
	dir_label.add_theme_font_size_override("font_size", 11)
	_box.add_child(dir_label)

	_hint("在左侧选择一个图层来编辑它的属性。")
	_hint("图层是树：容器（画布）里的子元素跟着容器一起移动缩放，容器还可以裁掉超出的部分。"
			+ "画布上点元素直接选中，Alt+点选最外层容器。")
	_hint("遮罩和滤镜是**独立图层**（「＋ 添加」里的遮罩层 / 滤镜层），"
			+ "插入在哪个图层上方就作用在它下面的图层上；在属性里可以改覆盖层数。")


func _apply_canvas(size: Vector2i) -> void:
	if main == null or main.doc == null:
		return
	main.doc.set_canvas(size, true)
	main.apply_change(true)


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
	visible.text = "启用这一层"
	visible.button_pressed = bool(layer.get("visible", true))
	visible.toggled.connect(func(on: bool) -> void:
		layer["visible"] = on
		main.apply_change()
	)
	_box.add_child(visible)

	if Layer.is_effect(layer):
		_build_cover(layer)
		if Layer.is_mask(layer):
			_build_mask_layer(layer)
		else:
			_build_filter_layer(layer)
		return

	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	_transform_row("X", "x", layer, 0.0, -20000.0, 20000.0, 1.0)
	_transform_row("Y", "y", layer, 0.0, -20000.0, 20000.0, 1.0)
	_size_row("宽", "w", layer, parent_size)
	_size_row("高", "h", layer, parent_size)
	_transform_row("旋转", "rotation", layer, 0.0, -3600.0, 3600.0, 1.0)
	_hint("X / Y / 宽 / 高 都是相对「父级」的像素：根图层相对画布，容器里的子元素相对容器框。"
			+ "画布上拖动元素即可移动；拖八个手柄缩放尺寸，角点按住 Shift 等比。")

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
		"group":
			_build_group(layer)
		_:
			_build_color(layer)


# ---------------------------------------------------------------- 覆盖范围

func _build_cover(layer: Dictionary) -> void:
	_section("覆盖范围")
	var cover: Dictionary = layer["cover"]
	# 下方真正能生成节点的兄弟数 = 区间编号的上限（隐藏的效果层不占位置）。
	var capacity: int = main.doc.cover_capacity(main.sel)
	var top := float(maxi(1, capacity))
	var mode_value := String(cover.get("mode", "all"))

	var mode := _option(COVER_MODES, COVER_MODE_NAMES, mode_value)
	_row("覆盖", mode)

	var count := _spin(float(int(cover.get("count", 1))), 1.0, top, 1.0)
	count.editable = mode_value == "count"
	count.tooltip_text = "紧挨着效果层下面的 N 层"
	_row("下方层数", count)

	# 区间："第 from 层到第 to 层"，1 = 紧邻效果层下面那一层，数字越大离它越远。
	var from_sp := _spin(float(int(cover.get("from", 1))), 1.0, top, 1.0)
	from_sp.tooltip_text = "起点：1 = 紧邻下方那一层"
	var to_sp := _spin(float(int(cover.get("to", 1))), 1.0, top, 1.0)
	to_sp.tooltip_text = "终点：包含这一层"
	var range_row := HBoxContainer.new()
	range_row.add_theme_constant_override("separation", 6)
	range_row.add_child(from_sp)
	range_row.add_child(to_sp)
	_row("第 … 到 … 层", range_row)
	# 只有"指定区间"模式下这两个数字才有意义，其余模式置灰（否则用户会以为改了没反应）。
	from_sp.editable = mode_value == "range"
	to_sp.editable = mode_value == "range"

	mode.item_selected.connect(func(i: int) -> void:
		var next_mode := String(COVER_MODES[i])
		if next_mode == "range":
			# 切到「指定区间」时先把区间设成"现在实际覆盖的那几层"，
			# 画面不变，再让用户往上/往下微调 —— 否则一点进来就被缩成 1 层，很吓人。
			cover["from"] = 1
			cover["to"] = maxi(1, main.doc.covered_count(main.sel))
		cover["mode"] = next_mode
		main.apply_change(true)
	)
	count.value_changed.connect(func(v: float) -> void:
		cover["mode"] = "count"
		cover["count"] = int(v)
		main.apply_change(true)
	)
	from_sp.value_changed.connect(func(v: float) -> void:
		cover["mode"] = "range"
		cover["from"] = int(v)
		# 起点超过终点时把终点一起推上去，避免出现空区间。
		cover["to"] = maxi(int(cover.get("to", 1)), int(v))
		main.apply_change(true)
	)
	to_sp.value_changed.connect(func(v: float) -> void:
		cover["mode"] = "range"
		cover["to"] = maxi(int(v), int(cover.get("from", 1)))
		main.apply_change(true)
	)

	var names := PackedStringArray(main.doc.covered_names(main.sel))
	var info := Label.new()
	if names.is_empty():
		info.text = "当前没有覆盖任何图层 —— 这一层暂时不生效。"
		info.modulate = Color(0.9, 0.72, 0.5)
	else:
		info.text = "当前覆盖 %d 层：%s" % [names.size(), "、".join(names)]
		info.modulate = Color(0.66, 0.7, 0.78)
	info.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	info.add_theme_font_size_override("font_size", 11)
	_box.add_child(info)
	_hint("效果层只作用在**同一父级、它下方**的图层上。用「指定区间」可以只盖中间某几层："
			+ "序号 1 = 紧挨着它的下一层，序号越大离它越远；没被盖到的层照常画在它上面。")


# ---------------------------------------------------------------- 遮罩层

func _build_mask_layer(layer: Dictionary) -> void:
	_section("遮罩形状")
	var shape := _option(Mask.SHAPES, SHAPE_NAMES, String(layer.get("shape", "rect")))
	shape.item_selected.connect(func(i: int) -> void:
		layer["shape"] = Mask.SHAPES[i]
		main.apply_change(true)
	)
	_row("形状", shape)

	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	_frame_row("X", "x", layer, parent_size, -20000.0, 20000.0, 1.0)
	_frame_row("Y", "y", layer, parent_size, -20000.0, 20000.0, 1.0)
	_frame_row("宽", "w", layer, parent_size, Frame.MIN_SIZE, 20000.0, 1.0)
	_frame_row("高", "h", layer, parent_size, Frame.MIN_SIZE, 20000.0, 1.0)

	var radius := _spin(float(layer.get("radius", 0.0)), 0.0, 4000.0, 1.0)
	radius.value_changed.connect(func(v: float) -> void:
		layer["radius"] = v
		main.apply_change()
	)
	_row("圆角", radius)

	var feather := _spin(float(layer.get("feather", 0.0)), 0.0, 2000.0, 1.0)
	feather.value_changed.connect(func(v: float) -> void:
		layer["feather"] = v
		main.apply_change()
	)
	_row("羽化", feather)

	var invert := CheckButton.new()
	invert.text = "反相（只保留遮罩外面的部分）"
	invert.button_pressed = bool(layer.get("invert", false))
	invert.toggled.connect(func(v: bool) -> void:
		layer["invert"] = v
		main.apply_change()
	)
	_box.add_child(invert)

	if String(layer.get("shape", "rect")) == "image":
		var label := Label.new()
		var src := String(layer.get("src", ""))
		label.text = src if not src.is_empty() else "（未选择遮罩图）"
		label.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
		label.modulate = Color(0.7, 0.73, 0.8)
		label.add_theme_font_size_override("font_size", 11)
		_row("遮罩图", label)
		var pick := Button.new()
		pick.text = "选择遮罩图…"
		pick.pressed.connect(func() -> void: _pick_texture(layer, "src"))
		_row("替换", pick)

	_hint("遮罩框就是这一层的选区框：在画布上拖黄框即可改位置/大小（宽高留 0 = 覆盖整个父级）。"
			+ "遮罩作用在它覆盖的那些图层**合成后**的结果上，所以文字/图片/粒子一视同仁。")


# ---------------------------------------------------------------- 滤镜层

func _build_filter_layer(layer: Dictionary) -> void:
	_section("滤镜")
	var f: Dictionary = layer["filters"]

	_filter_on("色彩调节（亮度 / 对比度 / 饱和度 / 色相）", f, Filters.COLOR)
	_filter_spin(f, Filters.COLOR, "brightness", "亮度", -1.0, 1.0, 0.01)
	_filter_spin(f, Filters.COLOR, "contrast", "对比度", 0.0, 3.0, 0.01)
	_filter_spin(f, Filters.COLOR, "saturation", "饱和度", 0.0, 3.0, 0.01)
	_filter_spin(f, Filters.COLOR, "hue", "色相（度）", -180.0, 180.0, 1.0)

	_filter_on("模糊（高斯）", f, Filters.BLUR)
	_filter_spin(f, Filters.BLUR, "radius", "半径（像素）", 0.0, 200.0, 1.0)

	_filter_on("暗角", f, Filters.VIGNETTE)
	_filter_spin(f, Filters.VIGNETTE, "amount", "强度", 0.0, 1.0, 0.01)
	_filter_spin(f, Filters.VIGNETTE, "radius", "起始半径", 0.0, 1.5, 0.01)
	_filter_spin(f, Filters.VIGNETTE, "softness", "柔和度", 0.01, 1.0, 0.01)

	_filter_on("色调分离", f, Filters.POSTERIZE)
	_filter_spin(f, Filters.POSTERIZE, "levels", "级数", 2.0, 64.0, 1.0)

	_filter_on("阴影", f, Filters.SHADOW)
	_filter_spin(f, Filters.SHADOW, "x", "偏移 X", -500.0, 500.0, 1.0)
	_filter_spin(f, Filters.SHADOW, "y", "偏移 Y", -500.0, 500.0, 1.0)
	_filter_spin(f, Filters.SHADOW, "alpha", "不透明度", 0.0, 1.0, 0.01)
	_filter_color(f, Filters.SHADOW, "颜色")

	_filter_on("发光", f, Filters.GLOW)
	_filter_spin(f, Filters.GLOW, "radius", "半径（像素）", 0.0, 200.0, 1.0)
	_filter_spin(f, Filters.GLOW, "alpha", "不透明度", 0.0, 1.0, 0.01)
	_filter_color(f, Filters.GLOW, "颜色")

	_filter_on("描边", f, Filters.STROKE)
	_filter_spin(f, Filters.STROKE, "width", "宽度（像素）", 0.0, 64.0, 0.5)
	_filter_color(f, Filters.STROKE, "颜色")

	_hint("这一层对下方被覆盖的图层**合成后**整体生效："
			+ "色彩 / 暗角 / 色调分离是逐像素着色器（几乎零开销）；模糊每层多两趟渲染；"
			+ "阴影 / 发光 / 描边是把被覆盖的内容染色复制一份垫在下面（粒子与更内层的效果层不复制）。"
			+ "勾选任意一组才会生效 —— 没勾的组在着色器里就是恒等运算。")


# ---------------------------------------------------------------- 内容图层：类型专属

func _build_group(layer: Dictionary) -> void:
	_section("容器")
	var clip := CheckButton.new()
	clip.text = "裁掉超出容器框的部分"
	clip.button_pressed = bool(layer.get("clip", true))
	clip.toggled.connect(func(on: bool) -> void:
		layer["clip"] = on
		main.apply_change()
	)
	_box.add_child(clip)
	var kids = layer.get("children", [])
	var count: int = (kids as Array).size() if kids is Array else 0
	var info := Label.new()
	info.text = "子图层：%d 个" % count
	info.modulate = Color(0.62, 0.66, 0.74)
	info.add_theme_font_size_override("font_size", 11)
	_box.add_child(info)
	_hint("容器改尺寸时子元素按同一比例拉伸；想看内容原尺寸就把容器框拖回去。"
			+ "选中容器后点「＋ 添加」会把新图层加进容器里。")


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

	_hint("粒子贴图留空时使用内置白色圆点，导出时会内联进场景。"
			+ "粒子被效果层复制类滤镜（阴影/描边/发光）跳过，遮罩/模糊/调色照常生效。")


# ---------------------------------------------------------------- 滤镜控件

func _filter_on(label_text: String, f: Dictionary, key: String) -> void:
	var g: Dictionary = f[key]
	var cb := CheckButton.new()
	cb.text = label_text
	cb.button_pressed = bool(g.get("enabled", false))
	cb.toggled.connect(func(v: bool) -> void:
		g["enabled"] = v
		main.apply_change()
	)
	_box.add_child(cb)


func _filter_spin(f: Dictionary, key: String, param: String, label_text: String,
		minv: float, maxv: float, step: float) -> void:
	var g: Dictionary = f[key]
	var sp := _spin(float(g.get(param, minv)), minv, maxv, step)
	sp.value_changed.connect(func(v: float) -> void:
		g[param] = v
		main.apply_change()
	)
	_row("  " + label_text, sp)


func _filter_color(f: Dictionary, key: String, label_text: String) -> void:
	var g: Dictionary = f[key]
	var c := _color(Layer.to_color(g.get("color"), Color.WHITE))
	c.color_changed.connect(func(col: Color) -> void:
		g["color"] = col
		main.apply_change()
	)
	_row("  " + label_text, c)


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


## 宽/高。没定过尺寸的图层显示的是父级尺寸；第一次改值时才把框落成显式数字
## （Frame.materialize），这样打开的旧文档不会被无声改写。
func _size_row(label_text: String, key: String, layer: Dictionary, parent_size: Vector2) -> void:
	var r := Frame.of(layer, parent_size)
	var sp := _spin(r.size.x if key == "w" else r.size.y, Frame.MIN_SIZE, 20000.0, 1.0)
	sp.value_changed.connect(func(v: float) -> void:
		Frame.materialize(layer, parent_size)
		layer["transform"][key] = maxf(Frame.MIN_SIZE, v)
		main.apply_change()
	)
	_row(label_text, sp)


## 遮罩框的四条边（存在图层的 transform 里，和普通元素的选区框同一个模型）。
func _frame_row(label_text: String, key: String, layer: Dictionary, parent_size: Vector2,
		minv: float, maxv: float, step: float) -> void:
	var r := Frame.of(layer, parent_size)
	var value := r.position.x if key == "x" else r.position.y
	if key == "w":
		value = r.size.x
	elif key == "h":
		value = r.size.y
	var sp := _spin(value, minv, maxv, step)
	sp.value_changed.connect(func(v: float) -> void:
		if key == "x" or key == "y":
			Frame.materialize(layer, parent_size)
		layer["transform"][key] = maxf(Frame.MIN_SIZE, v) if (key == "w" or key == "h") else v
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
