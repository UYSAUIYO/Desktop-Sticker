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
const Timeline = preload("res://src/doc/timeline.gd")
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
const COVER_MODES := ["all", "count", "range", "layer"]
const COVER_MODE_NAMES := ["下方全部图层", "下方紧邻 N 层", "指定区间（第几层到第几层）",
		"绑定到指定图层"]
const PRESETS := [
	{"name": "1920 × 1080（1080p）", "size": Vector2i(1920, 1080)},
	{"name": "2560 × 1440（2K）", "size": Vector2i(2560, 1440)},
	{"name": "3840 × 2160（4K）", "size": Vector2i(3840, 2160)},
	{"name": "1080 × 1920（竖屏）", "size": Vector2i(1080, 1920)},
	{"name": "1080 × 1080（方形）", "size": Vector2i(1080, 1080)},
]
## AnimatedTexture 的帧数上限。超过这个数的帧不会播放，白占包体积。
const MAX_FRAMES := 256

var main = null

var _box: VBoxContainer
var _sig := ""
var _file_dialog: FileDialog
var _gif_dialog: FileDialog
var _dir_dialog: FileDialog
var _pick_layer: Dictionary = {}
var _pick_field := ""
## 当前显示"播放头处取值"的控件：[{channel, node}]。播放头一动就只改它们的值，
## 不重建面板（重建会把用户正在拖的滑块换掉）。
var _time_controls: Array = []


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
	_time_controls.clear()
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
	_scale_row(layer, parent_size)
	_hint("X / Y / 宽 / 高 都是相对「父级」的像素：根图层相对画布，容器里的子元素相对容器框。"
			+ "画布上拖动元素即可移动；拖八个手柄缩放尺寸，角点按住 Shift 等比。"
			+ "\n有关键帧的通道显示的是播放头处的值，改动会写进当前帧；"
			+ "没有关键帧的通道改的是基础值。")

	var opacity := _spin(_display_value(layer, "opacity", parent_size), 0.0, 1.0, 0.01)
	opacity.value_changed.connect(func(v: float) -> void:
		layer["transform"]["opacity"] = v
		_auto_key(layer, "opacity", v)
		main.apply_change()
	)
	_row("不透明度", opacity)
	_register_time(opacity, "opacity")

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

	# 绑定模式：从这棵树里挑一层，效果只作用在它身上（和图层顺序无关）。
	var candidates := _cover_candidates()
	var target_pick := OptionButton.new()
	target_pick.add_item("（未选择）", -1)
	for i in candidates.size():
		var item: Dictionary = candidates[i]
		var l: Dictionary = item["layer"]
		var depth := int(item["depth"])
		target_pick.add_item("　".repeat(depth) + String(l.get("name", "图层"))
				+ "    ·    " + String(TYPE_NAMES.get(String(l.get("type", "")), "图层")), i)
	var current_id := Layer.cover_target(layer)
	for i in candidates.size():
		if String((candidates[i]["layer"] as Dictionary).get("id", "")) == current_id:
			target_pick.select(i + 1)
			break
	_row("绑定图层", target_pick)

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
		if next_mode == "layer":
			# 切到「绑定」时先把目标定成"现在看起来被盖住的最上面那一层"，
			# 画面不变，再让用户改绑定对象。
			if main.doc.cover_target_path(main.sel).is_empty():
				cover["target"] = _top_covered_id()
		cover["mode"] = next_mode
		main.apply_change(true)
	)
	target_pick.item_selected.connect(func(i: int) -> void:
		# i 是**条目位置**：0 = 「未选择」，1..n 对应 candidates[0..n-1]。
		if i <= 0 or i > candidates.size():
			cover["mode"] = "layer"
			cover["target"] = ""
		else:
			cover["mode"] = "layer"
			cover["target"] = String((candidates[i - 1]["layer"] as Dictionary).get("id", ""))
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
	var bound := not Layer.cover_target(layer).is_empty()
	if names.is_empty():
		if bound:
			info.text = "绑定的图层已经不存在（或绑到了自己身上）—— 这一层暂时不生效。"
		else:
			info.text = "当前没有覆盖任何图层 —— 这一层暂时不生效。"
		info.modulate = Color(0.9, 0.72, 0.5)
	else:
		info.text = "当前覆盖 %d 层：%s" % [names.size(), "、".join(names)]
		info.modulate = Color(0.66, 0.7, 0.78)
	info.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	info.add_theme_font_size_override("font_size", 11)
	_box.add_child(info)
	_hint("「绑定到指定图层」= 只作用在那一个图层上，跟它在列表里的位置无关："
			+ "目标上下移动、中间插进别的图层都不影响。"
			+ "前三种模式按\"离这一层多远\"算：序号 1 = 紧挨着它的下一层，没被盖到的层照常画在它上面。")
	_hint("效果覆盖的区域 = 被覆盖图层自己的框。目标铺满整张画布（没设过宽高）时，"
			+ "滤镜自然就是全屏的 —— 想只做局部有三种办法：把目标拖小、把它放进容器，"
			+ "或者（滤镜层）直接拖画布上的绿框，把作用范围框小。")


## 可绑定的图层：**同一组兄弟**里的内容图层（和 doc.cover_target_path 的规则一致）。
## 绑定不跨容器 —— 效果层的坐标空间就是它所在父级的，跨容器会让遮罩框对不上；
## 想给容器里的元素加效果，选中那个元素再「＋ 添加」，效果层会自动加进同一个容器。
func _cover_candidates() -> Array:
	var out := []
	if main == null or main.doc == null or main.sel.is_empty():
		return out
	var parent: PackedInt32Array = main.doc.parent_path_of(main.sel)
	var arr: Array = main.doc.siblings_at(parent)
	for i in range(arr.size() - 1, -1, -1):
		var layer: Dictionary = arr[i]
		if Layer.is_effect(layer):
			continue
		var path := parent.duplicate()
		path.append(i)
		out.append({"path": path, "layer": layer, "depth": 0})
	return out


## 当前"看起来被盖住"的最上面那一层的 id（切到绑定模式时用它保持画面不变）。
func _top_covered_id() -> String:
	var span: Vector2i = main.doc.cover_span(main.sel)
	var arr: Array = main.doc.siblings_at(main.doc.parent_path_of(main.sel))
	var idx := span.y - 1
	if idx >= 0 and idx < arr.size() and arr[idx] is Dictionary:
		return String((arr[idx] as Dictionary).get("id", ""))
	return ""


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

## 滤镜层的「位置与尺寸（作用区域）」。
##
## 滤镜层的框**一直存在**，和图片 / 遮罩层一样：画布上直接拖、拖手柄缩放、
## 改这里的 X·Y·宽·高，不需要先勾任何开关。两种状态：
##   · 跟随被覆盖图层（默认）—— 框 = 被覆盖内容的包围盒（画布上画虚线），
##     滤镜作用在整块内容上；内容移动/改尺寸时框跟着走。
##   · 固定范围 —— 框就是它自己的 transform，框内是滤镜后的样子，框外是原图。
## 第一次拖框 / 改数字 / 改形状就落成固定范围（画面先不变，框正好等于现在的包围盒），
## 想回到跟随点下面的按钮。
func _build_filter_region(layer: Dictionary) -> void:
	_section("位置与尺寸（作用区域）")
	var rg: Dictionary = Filters.region(layer)
	layer["region"] = rg
	var fixed := bool(rg.get("enabled", false))

	var state := Label.new()
	state.text = ("固定范围：框内是滤镜后的样子，框外是原图。" if fixed
			else "跟随被覆盖图层（画布上是虚线框）：拖动框或改下面的数字，就会固定成你框出来的范围。")
	state.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	state.modulate = Color(0.62, 0.8, 0.68) if fixed else Color(0.66, 0.7, 0.78)
	state.add_theme_font_size_override("font_size", 11)
	_box.add_child(state)

	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	_frame_row("X", "x", layer, parent_size, -20000.0, 20000.0, 1.0, true)
	_frame_row("Y", "y", layer, parent_size, -20000.0, 20000.0, 1.0, true)
	_frame_row("宽", "w", layer, parent_size, Frame.MIN_SIZE, 20000.0, 1.0, true)
	_frame_row("高", "h", layer, parent_size, Frame.MIN_SIZE, 20000.0, 1.0, true)

	if not fixed:
		_hint("框正好盖住现在被处理的那块内容；把它拖小 / 拖到别处，滤镜就只在那儿生效。"
				+ "没固定时形状（矩形/椭圆…）用不上 —— 框固定之后才出现。")
		return

	var shapes: Array = Filters.REGION_SHAPES
	var shape_names := ["矩形", "圆角矩形", "椭圆"]
	var shape := _option(shapes, shape_names, String(rg.get("shape", "rect")))
	shape.item_selected.connect(func(i: int) -> void:
		rg["shape"] = String(shapes[i])
		main.apply_change(true)
	)
	_row("形状", shape)

	var radius := _spin(float(rg.get("radius", 0.0)), 0.0, 4000.0, 1.0)
	radius.value_changed.connect(func(v: float) -> void:
		rg["radius"] = v
		main.apply_change()
	)
	_row("圆角", radius)

	var feather := _spin(float(rg.get("feather", 0.0)), 0.0, 2000.0, 1.0)
	feather.value_changed.connect(func(v: float) -> void:
		rg["feather"] = v
		main.apply_change()
	)
	_row("羽化", feather)

	var invert := CheckButton.new()
	invert.text = "反相（只在框**外**应用滤镜）"
	invert.button_pressed = bool(rg.get("invert", false))
	invert.toggled.connect(func(v: bool) -> void:
		rg["invert"] = v
		main.apply_change()
	)
	_box.add_child(invert)

	var follow := Button.new()
	follow.text = "改回「跟随被覆盖图层」"
	follow.pressed.connect(func() -> void:
		rg["enabled"] = false
		main.apply_change(true)
	)
	_box.add_child(follow)

	_hint("框内是滤镜后的样子，框外是原图（对模糊/调色/暗角/色调分离都成立）；"
			+ "阴影/发光/描边这些副本也只在框内出现。")


func _build_filter_layer(layer: Dictionary) -> void:
	_build_filter_region(layer)
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
			+ "勾选任意一组才会生效 —— 没勾的组在着色器里就是恒等运算。"
			+ "只想让滤镜作用在画面的一块上，用上面的「作用区域」。")


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

	_build_animation(layer)


## 动图：GIF 自动拆帧，或直接导入一个帧序列目录。帧序列存在图层自己的 frames 里，
## 导出时逐帧进包，运行时用 AnimatedTexture 播（见 layer_nodes.DSTK_IMAGE_SCRIPT）。
func _build_animation(layer: Dictionary) -> void:
	_section("动图")
	var frames = layer.get("frames", [])
	var has_frames: bool = frames is Array and not (frames as Array).is_empty()

	var info := Label.new()
	if has_frames:
		info.text = "%d 帧 · %.1f fps" % [(frames as Array).size(), float(layer.get("fps", 12.0))]
	else:
		info.text = "（未启用，按单张图片渲染）"
	info.modulate = Color(0.7, 0.73, 0.8)
	info.add_theme_font_size_override("font_size", 11)
	_row("帧序列", info)

	var menu := MenuButton.new()
	menu.text = "导入动图…"
	var popup := menu.get_popup()
	popup.add_item("导入 GIF（自动拆帧）", 0)
	popup.add_item("导入帧序列目录…", 1)
	popup.add_separator()
	popup.add_item("清除帧序列", 2)
	popup.id_pressed.connect(func(id: int) -> void: _on_animation_menu(id, layer))
	_row("操作", menu)

	if has_frames:
		var fps := _spin(float(layer.get("fps", 12.0)), 0.5, 60.0, 1.0)
		fps.value_changed.connect(func(v: float) -> void:
			layer["fps"] = v
			main.apply_change()
		)
		_row("帧率", fps)

		var speed := _spin(float(layer.get("speed", 1.0)), 0.1, 8.0, 0.1)
		speed.value_changed.connect(func(v: float) -> void:
			layer["speed"] = v
			main.apply_change()
		)
		_row("倍速", speed)
		_hint("帧序列优先于「文件」。按文件名排序，建议用 frame_0001.png 这种补零命名；"
				+ "最多 256 帧，超出部分导入时截掉。")


func _on_animation_menu(id: int, layer: Dictionary) -> void:
	if id == 2:
		layer["frames"] = []
		main.apply_change(true)
		return
	if main.ensure_project_dir().is_empty():
		main.show_error("无法导入动图", "工程目录不可写。")
		return
	_pick_layer = layer
	if id == 0:
		if _gif_dialog == null:
			_gif_dialog = FileDialog.new()
			_gif_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
			_gif_dialog.access = FileDialog.ACCESS_FILESYSTEM
			_gif_dialog.title = "选择 GIF（会用 ffmpeg 拆成 PNG 帧）"
			_gif_dialog.filters = PackedStringArray(["*.gif ; GIF 动图"])
			_gif_dialog.file_selected.connect(_on_gif_selected)
			add_child(_gif_dialog)
		_gif_dialog.popup_centered_ratio(0.7)
	else:
		if _dir_dialog == null:
			_dir_dialog = FileDialog.new()
			_dir_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
			_dir_dialog.access = FileDialog.ACCESS_FILESYSTEM
			_dir_dialog.title = "选择帧序列目录（按文件名排序）"
			_dir_dialog.dir_selected.connect(_on_frames_dir_selected)
			add_child(_dir_dialog)
		_dir_dialog.popup_centered_ratio(0.7)


func _on_gif_selected(path: String) -> void:
	if _pick_layer.is_empty():
		return
	var res: Dictionary = AssetImport.import_gif(main.ensure_project_dir(), path)
	if not res.ok:
		main.show_error("GIF 导入失败", String(res.error))
		return
	var frames := _trim_frames(res.frames)
	_pick_layer["frames"] = frames
	_pick_layer["fps"] = float(res.fps)
	# 首帧同时当静态图：海报、以及"帧全丢"时的兜底都用它。
	_pick_layer["src"] = String(frames[0]) if not frames.is_empty() else ""
	_pick_layer = {}
	main.apply_change(true)


func _on_frames_dir_selected(path: String) -> void:
	if _pick_layer.is_empty():
		return
	var frames := _trim_frames(AssetImport.import_frames(main.ensure_project_dir(), path,
			path.get_file()))
	if frames.is_empty():
		main.show_error("帧序列导入失败", "这个目录里没有可用的图片：\n" + path)
		return
	_pick_layer["frames"] = frames
	_pick_layer["src"] = String(frames[0])
	_pick_layer = {}
	main.apply_change(true)


## 截到引擎上限。超出的帧不播放却会被打进包里，所以在导入时就截断并明确告知。
func _trim_frames(frames: Array) -> Array:
	if frames.size() <= MAX_FRAMES:
		return frames
	main.show_message("帧数过多",
			"动图最多播放 %d 帧，只保留前 %d 帧。\n" % [MAX_FRAMES, MAX_FRAMES])
	return frames.slice(0, MAX_FRAMES)


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
		_file_dialog.file_selected.connect(_on_texture_selected)
		add_child(_file_dialog)
	# 只有图片图层的 src 认识 GIF（会拆帧）；粒子贴图/遮罩图不能选它。
	_file_dialog.filters = AssetImport.image_filters(field == "src"
			and String(layer.get("type", "")) == "image")
	_file_dialog.current_dir = dir
	_file_dialog.popup_centered_ratio(0.7)


func _on_texture_selected(path: String) -> void:
	if _pick_layer.is_empty() or _pick_field.is_empty():
		return
	if path.get_extension().to_lower() == "gif" and _pick_field == "src" \
			and String(_pick_layer.get("type", "")) == "image":
		var res: Dictionary = AssetImport.import_gif(main.ensure_project_dir(), path)
		if not res.ok:
			main.show_error("GIF 导入失败", String(res.error))
			return
		var frames := _trim_frames(res.frames)
		_pick_layer["frames"] = frames
		_pick_layer["fps"] = float(res.fps)
		_pick_layer["src"] = String(frames[0]) if not frames.is_empty() else ""
		_pick_layer = {}
		_pick_field = ""
		main.apply_change(true)
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
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	var sp := _spin(_display_value(layer, key, parent_size), minv, maxv, step)
	sp.value_changed.connect(func(v: float) -> void:
		tr[key] = v
		_auto_key(layer, key, v)
		main.apply_change()
	)
	_row(label_text, sp)
	_register_time(sp, key)


## 缩放：静态路径不读 transform.scale（历史字段已折算进 w/h），
## 所以缩放**只能**用关键帧表达 —— 改这个数字就是写播放头的 key，顺带建出轨道。
func _scale_row(layer: Dictionary, parent_size: Vector2) -> void:
	var sp := _spin(_display_value(layer, "scale", parent_size), 0.05, 10.0, 0.05)
	sp.value_changed.connect(func(v: float) -> void:
		Timeline.set_key(layer, "scale", main._canvas.time, v)
		layer["transform"]["scale"] = 1.0
		main.mark_dirty()
		main.apply_change()
	)
	_row("缩放", sp)
	_register_time(sp, "scale")


# ---------------------------------------------------------------- 播放头

## 让"显示播放头值"的控件跟上时间。播放中每帧调用，只写值不重建面板。
func sync_time() -> void:
	if main == null or main.doc == null or main._canvas == null:
		return
	var layer: Dictionary = main.selected_layer()
	if layer.is_empty():
		return
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	for item in _time_controls:
		var node = item.get("node")
		if not is_instance_valid(node) or not (node is SpinBox):
			continue
		(node as SpinBox).set_value_no_signal(_display_value(layer, String(item["channel"]),
				parent_size))


## 通道在当前播放头处的**界面值**：有关键帧就取采样值（x/y 换成像素），否则取基础值。
func _display_value(layer: Dictionary, channel: String, parent_size: Vector2) -> float:
	var tracks := Timeline.tracks_of(layer)
	if not Timeline.find_track(tracks, channel).is_empty():
		var v := Timeline.value_at(layer, channel, main._canvas.time, parent_size)
		return Timeline.to_px(channel, v, parent_size)
	if channel == "scale":
		return float(layer.get("transform", {}).get("scale", 1.0))
	return float(layer.get("transform", {}).get(channel, 0.0))


func _register_time(control: Control, channel: String) -> void:
	_time_controls.append({"channel": channel, "node": control})


## 自动关键帧（属性面板这一侧）：通道已经有关键帧时，改值就是改**播放头那一帧**，
## 而不是改一个播放时会被覆盖的基础值。没有轨道的通道保持原行为（只改基础值）。
func _auto_key(layer: Dictionary, channel: String, value: float) -> void:
	if main == null or main._canvas == null:
		return
	if Timeline.find_track(Timeline.tracks_of(layer), channel).is_empty():
		return
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	Timeline.set_key(layer, channel, main._canvas.time,
			Timeline.to_doc(channel, value, parent_size))


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


## 遮罩框 / 滤镜框的四条边（存在图层的 transform 里，和普通元素的选区框同一个模型）。
## pin_region = true（滤镜层）：显示的是 doc.filter_frame()（没固定时 = 被覆盖内容的
## 包围盒），第一次改值就把它落成固定范围 —— 和拖画布上的框是同一个动作。
func _frame_row(label_text: String, key: String, layer: Dictionary, parent_size: Vector2,
		minv: float, maxv: float, step: float, pin_region: bool = false) -> void:
	var r: Rect2 = main.doc.filter_frame(main.sel) if pin_region else Frame.of(layer, parent_size)
	var value: float = r.position.x if key == "x" else r.position.y
	if key == "w":
		value = r.size.x
	elif key == "h":
		value = r.size.y
	var sp := _spin(value, minv, maxv, step)
	sp.value_changed.connect(func(v: float) -> void:
		if pin_region:
			if not Filters.region_enabled(layer):
				main.doc.fix_filter_region(main.sel)
			layer["transform"][key] = maxf(Frame.MIN_SIZE, v) if (key == "w" or key == "h") else v
			main.apply_change(true)     # 固定后要重建面板：形状/圆角/羽化这时才出现
			return
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
