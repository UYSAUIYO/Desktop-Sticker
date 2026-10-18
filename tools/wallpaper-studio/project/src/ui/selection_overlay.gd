extends Control
## 画布上的交互层：选区框、八个手柄，以及鼠标拖动。
##
## 用的都是 Godot 内建的东西：Control 的锚点/尺寸、_gui_input、
## draw_line/draw_rect、mouse_default_cursor_shape。
## 几何计算全部在 src/doc/frame.gd —— 纯函数、有无头用例钉住，这里只做坐标换算。
##
## 坐标分层要说清（这里是唯一容易错的地方）：
##   · 屏幕坐标   —— 本控件（= 画布区）的局部坐标；
##   · 父空间像素 —— 图层 transform 的坐标系：根层是画布，子层是容器的框。
## space 字典统一描述"某个空间 -> 屏幕"：{origin, scale, rotation, rect}。
##
## 遮罩层同样是普通图层：它的框就是遮罩形状的位置尺寸，所以拖它=改遮罩（画黄框）。
## 滤镜层也**一直有框**（画绿框），但有两种状态：没固定时框跟着被覆盖的内容走（虚线），
## 拖它/改尺寸就落成"固定范围"（实线）—— 框内是滤镜后的样子，框外保持原样。
##
## 手感上的要点：拖动过程中**不能**用 event.position 算位移。控件/元素自己的位置
## 每帧都在变，局部坐标会跟着跳；统一用 get_global_mouse_position() 的差值。

const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")

const BORDER_COLOR := Color(0.42, 0.62, 0.95)
const MASK_COLOR := Color(0.96, 0.72, 0.24)
const REGION_COLOR := Color(0.36, 0.85, 0.62)
const HANDLE_SIZE := 9.0

var main = null
var canvas = null

var _brush := -1             # 0 = 拖位置；其余是 Frame 的手柄位掩码
var _pressed := false
var _start_mouse := Vector2.ZERO
var _start_rect := Rect2()
var _start_children := []


func setup(m, c) -> void:
	main = m
	canvas = c


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	mouse_default_cursor_shape = Control.CURSOR_ARROW
	resized.connect(queue_redraw)


# ---------------------------------------------------------------- 状态

func _layer() -> Dictionary:
	if main == null or main.doc == null or main.sel.is_empty():
		return {}
	return main.doc.layer_at(main.sel)


## 元素框所在空间（父空间）-> 屏幕。框在父空间里是**轴对齐**的：
## 图层的旋转只转内容，不改它的框（渲染端也是绕框的左上角转）。
func _space() -> Dictionary:
	if main == null or main.doc == null or main.sel.is_empty():
		return {}
	var map := Frame.space_map(main.doc, main.sel)
	var d: float = canvas.display_scale()
	return {
		"origin": canvas.container_origin() + (map["origin"] as Vector2) * d,
		"scale": float(map["scale"]) * d,
		"rotation": float(map["rotation"]),
		"rect": main.doc.filter_frame(main.sel),
	}


func _box_color(layer: Dictionary) -> Color:
	if Layer.is_mask(layer):
		return MASK_COLOR
	if Layer.is_filter(layer):
		return REGION_COLOR
	return BORDER_COLOR


## 这一层在画布上有没有"可拖的框"：内容图层、遮罩层、滤镜层都有。
## 滤镜层的框一直存在（跟随被覆盖内容 / 固定范围），所以从第一次点选起就能拖 ——
## 不需要先勾任何开关（"像图片一样调位置大小"）。
func _frameable(layer: Dictionary) -> bool:
	return not layer.is_empty()


# ---------------------------------------------------------------- 坐标换算

func _to_screen(space: Dictionary, p: Vector2) -> Vector2:
	return (space["origin"] as Vector2) + (p * float(space["scale"])).rotated(
			deg_to_rad(float(space["rotation"])))


func _to_local(space: Dictionary, screen_p: Vector2) -> Vector2:
	return ((screen_p - (space["origin"] as Vector2)) / maxf(0.0001, float(space["scale"]))) \
			.rotated(-deg_to_rad(float(space["rotation"])))


func _corners(space: Dictionary) -> Array:
	var r: Rect2 = space["rect"]
	return [r.position, Vector2(r.end.x, r.position.y), r.end,
			Vector2(r.position.x, r.end.y)]


## 第 i 个手柄的中心（屏幕坐标）。顺序与 Frame.HANDLES 一致。
func _handle_point(space: Dictionary, i: int) -> Vector2:
	var r: Rect2 = space["rect"]
	var h: int = Frame.HANDLES[i]
	var x := r.position.x + r.size.x * 0.5
	if (h & Frame.LEFT) != 0:
		x = r.position.x
	elif (h & Frame.RIGHT) != 0:
		x = r.position.x + r.size.x
	var y := r.position.y + r.size.y * 0.5
	if (h & Frame.TOP) != 0:
		y = r.position.y
	elif (h & Frame.BOTTOM) != 0:
		y = r.position.y + r.size.y
	return _to_screen(space, Vector2(x, y))


func _handle_at(space: Dictionary, screen_p: Vector2) -> int:
	for i in Frame.HANDLES.size():
		if screen_p.distance_to(_handle_point(space, i)) <= HANDLE_SIZE:
			return int(Frame.HANDLES[i])
	return -1


func _body_at(space: Dictionary, screen_p: Vector2) -> bool:
	var r: Rect2 = space["rect"]
	return Rect2(Vector2.ZERO, r.size).has_point(_to_local(space, screen_p) - r.position)


# ---------------------------------------------------------------- 绘制

func _draw() -> void:
	var layer := _layer()
	if not _frameable(layer):
		return
	var space := _space()
	if space.is_empty():
		return
	# 滤镜层还没固定范围时画虚线：框是跟着被覆盖内容走的，不是它自己的固定框。
	var auto_follow := Layer.is_filter(layer) and not Filters.region_enabled(layer)
	_draw_box(space, _box_color(layer), true, auto_follow)


func _draw_box(space: Dictionary, color: Color, with_handles: bool,
		dashed: bool = false) -> void:
	var pts := _corners(space)
	var screen_pts := []
	for p in pts:
		screen_pts.append(_to_screen(space, p))
	for i in 4:
		if dashed:
			_draw_dashed(screen_pts[i], screen_pts[(i + 1) % 4], color)
		else:
			draw_line(screen_pts[i], screen_pts[(i + 1) % 4], color, 1.0)
	if not with_handles:
		return
	for i in Frame.HANDLES.size():
		var c := _handle_point(space, i)
		var r := Rect2(c - Vector2(HANDLE_SIZE, HANDLE_SIZE) * 0.5,
				Vector2(HANDLE_SIZE, HANDLE_SIZE))
		draw_rect(r, Color(0.09, 0.1, 0.12, 0.92))
		draw_rect(r, color, false, 1.0)


## 虚线：一段实线 + 一段空白。用来说明"这个框还不是固定范围"。
func _draw_dashed(a: Vector2, b: Vector2, color: Color) -> void:
	var total := a.distance_to(b)
	if total <= 0.001:
		return
	var dir := (b - a) / total
	var t := 0.0
	while t < total:
		var e := minf(t + 6.0, total)
		draw_line(a + dir * t, a + dir * e, color, 1.0)
		t = e + 4.0


# ---------------------------------------------------------------- 输入

func _gui_input(event: InputEvent) -> void:
	if main == null or main.doc == null:
		return

	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if mb.pressed:
			if not _try_start_drag(mb.position):
				# 空白处 / 别的元素：改选中（Alt = 选最外层容器）。
				var path: PackedInt32Array = canvas.outer_group_at_screen(mb.position) \
						if mb.alt_pressed else canvas.layer_at_screen(mb.position)
				main.select_layer_path(path)
			accept_event()
		elif _pressed:
			_end_drag()
			accept_event()
		return

	if event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		if _pressed:
			_drag(mm.shift_pressed)
			accept_event()
		else:
			_update_cursor(mm.position)


func _update_cursor(screen_p: Vector2) -> void:
	var layer := _layer()
	if not _frameable(layer):
		mouse_default_cursor_shape = Control.CURSOR_ARROW
		return
	var space := _space()
	if space.is_empty():
		mouse_default_cursor_shape = Control.CURSOR_ARROW
		return
	var h := _handle_at(space, screen_p)
	if h >= 0:
		mouse_default_cursor_shape = Frame.cursor_for(h)
	elif _body_at(space, screen_p):
		mouse_default_cursor_shape = Control.CURSOR_MOVE
	else:
		mouse_default_cursor_shape = Control.CURSOR_ARROW


func _try_start_drag(screen_p: Vector2) -> bool:
	var layer := _layer()
	if not _frameable(layer):
		return false
	var space := _space()
	if space.is_empty():
		return false
	var handle := _handle_at(space, screen_p)
	if handle < 0 and not _body_at(space, screen_p):
		return false

	if Layer.is_filter(layer) and not Filters.region_enabled(layer):
		# 第一次碰滤镜层：先把框落成"现在被处理的那块"并固定范围 —— 拖的就是眼前
		# 这个框，位置不会跳。这里必须整棵重建（apply_change）：带模糊的滤镜只在
		# 构建时判定区域，要重建才会多出"框外显示原图"那层混合。
		main.doc.fix_filter_region(main.sel)
		main.apply_change(true)
		layer = _layer()
	else:
		Frame.materialize(layer, main.doc.parent_size(main.sel))
	space = _space()
	if Layer.is_group(layer):
		# 容器改尺寸会按比例改写子层的框，所以先拍一份快照，拖动时从快照重算
		# （否则每帧都在上一帧的结果上再乘一次，比例会滚雪球）。
		_start_children = (layer.get("children", []) as Array).duplicate(true)

	_brush = 0 if handle < 0 else handle
	_start_rect = space["rect"]
	_start_mouse = get_global_mouse_position()
	_pressed = true
	mouse_default_cursor_shape = Frame.cursor_for(_brush)
	queue_redraw()
	return true


func _drag(keep_ratio: bool) -> void:
	var layer := _layer()
	if layer.is_empty():
		return
	var space := _space()
	if space.is_empty():
		return
	var gdelta := get_global_mouse_position() - _start_mouse
	var delta := (gdelta / maxf(0.0001, float(space["scale"]))).rotated(
			-deg_to_rad(float(space["rotation"])))

	var t: Dictionary = layer["transform"]
	if _brush == 0:
		var moved := _start_rect.position + delta
		t["x"] = moved.x
		t["y"] = moved.y
	else:
		var nr := Frame.resized_rect(_start_rect, _brush, delta, keep_ratio)
		t["x"] = nr.position.x
		t["y"] = nr.position.y
		t["w"] = nr.size.x
		t["h"] = nr.size.y
		if Layer.is_group(layer):
			var sx := nr.size.x / maxf(1.0, _start_rect.size.x)
			var sy := nr.size.y / maxf(1.0, _start_rect.size.y)
			var anchor := Frame.anchor_point(_brush,
					Rect2(Vector2.ZERO, _start_rect.size))
			layer["children"] = Frame.scale_children(_start_children, sx, sy, anchor)

	main.apply_live()
	queue_redraw()


func _end_drag() -> void:
	_pressed = false
	_brush = -1
	_start_children = []
	main.apply_change(true)
