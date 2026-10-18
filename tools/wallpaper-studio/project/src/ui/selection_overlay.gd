extends Control
## 画布上的交互层：选区框、八个手柄、遮罩框，以及鼠标拖动。
##
## 用的都是 Godot 内建的东西：Control 的锚点/尺寸、_gui_input、
## draw_line/draw_rect、mouse_default_cursor_shape。
## 几何计算全部在 src/doc/frame.gd —— 纯函数、有无头用例钉住，这里只做坐标换算。
##
## 坐标分层要说清（这里是唯一容易错的地方）：
##   · 屏幕坐标   —— 本控件（= 画布区）的局部坐标；
##   · 父空间像素 —— 图层 transform 的坐标系：根层是画布，子层是容器的框；
##   · 图层本地像素 —— 遮罩用这套坐标（相对图层左上角）。
## space 字典统一描述"某个空间 -> 屏幕"：{origin, scale, rotation, rect}。
##
## 手感上的要点：拖动过程中**不能**用 event.position 算位移。控件/元素自己的位置
## 每帧都在变，局部坐标会跟着跳；统一用 get_global_mouse_position() 的差值。

const Frame = preload("res://src/doc/frame.gd")
const Mask = preload("res://src/doc/mask.gd")
const Layer = preload("res://src/doc/layer.gd")

const BORDER_COLOR := Color(0.42, 0.62, 0.95)
const BORDER_DIM := Color(0.42, 0.62, 0.95, 0.4)
const MASK_COLOR := Color(0.96, 0.72, 0.24)
const HANDLE_SIZE := 9.0

var main = null
var canvas = null

var _mode := 0               # 0 = 无；1 = 元素；2 = 遮罩
var _handle := -1            # 0 = 拖位置；其余是 Frame 的手柄位掩码
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


func _editing_mask() -> bool:
	if main == null:
		return false
	var layer := _layer()
	if layer.is_empty() or not main.mask_edit:
		return false
	var mask: Dictionary = layer.get("mask", {})
	return bool(mask.get("enabled", false))


## 元素框所在空间（父空间）-> 屏幕。框在父空间里是**轴对齐**的：
## 图层的旋转只转内容，不改它的框（渲染端也是绕框的左上角转）。
func _element_space() -> Dictionary:
	if main == null or main.doc == null or main.sel.is_empty():
		return {}
	var map := Frame.space_map(main.doc, main.sel)
	var d: float = canvas.display_scale()
	return {
		"origin": canvas.container_origin() + (map["origin"] as Vector2) * d,
		"scale": float(map["scale"]) * d,
		"rotation": float(map["rotation"]),
		"rect": Frame.of(_layer(), main.doc.parent_size(main.sel)),
	}


## 遮罩框：图层本地像素 -> 屏幕。旋转要算上图层自己的旋转（遮罩跟着内容转）。
func _mask_space() -> Dictionary:
	var layer := _layer()
	if layer.is_empty():
		return {}
	var map := Frame.layer_map(main.doc, main.sel)
	var d: float = canvas.display_scale()
	var size := Frame.of(layer, main.doc.parent_size(main.sel)).size
	return {
		"origin": canvas.container_origin() + (map["origin"] as Vector2) * d,
		"scale": float(map["scale"]) * d,
		"rotation": float(map["rotation"]) + float(layer["transform"].get("rotation", 0.0)),
		"rect": Mask.rect(layer.get("mask", {}), size),
		"size": size,
	}


func _active_space() -> Dictionary:
	return _mask_space() if _editing_mask() else _element_space()


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
	if layer.is_empty():
		return
	var editing := _editing_mask()
	var es := _element_space()
	if not es.is_empty():
		_draw_box(es, BORDER_DIM if editing else BORDER_COLOR, not editing)
	if editing:
		var ms := _mask_space()
		if not ms.is_empty():
			_draw_box(ms, MASK_COLOR, true)


func _draw_box(space: Dictionary, color: Color, with_handles: bool) -> void:
	var pts := _corners(space)
	var screen_pts := []
	for p in pts:
		screen_pts.append(_to_screen(space, p))
	for i in 4:
		draw_line(screen_pts[i], screen_pts[(i + 1) % 4], color, 1.0)
	if not with_handles:
		return
	for i in Frame.HANDLES.size():
		var c := _handle_point(space, i)
		var r := Rect2(c - Vector2(HANDLE_SIZE, HANDLE_SIZE) * 0.5,
				Vector2(HANDLE_SIZE, HANDLE_SIZE))
		draw_rect(r, Color(0.09, 0.1, 0.12, 0.92))
		draw_rect(r, color, false, 1.0)


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
	var space := _active_space()
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
	if layer.is_empty():
		return false
	var space := _active_space()
	if space.is_empty():
		return false
	var editing := _editing_mask()
	var handle := _handle_at(space, screen_p)
	if handle < 0 and not _body_at(space, screen_p):
		return false

	if editing:
		# 第一次拖遮罩：宽高还是 0（= 整层）的先落成显式数字。
		var mask: Dictionary = layer["mask"]
		if float(mask.get("w", 0.0)) <= 0.0 or float(mask.get("h", 0.0)) <= 0.0:
			var size: Vector2 = space["size"]
			mask["x"] = 0.0
			mask["y"] = 0.0
			mask["w"] = size.x
			mask["h"] = size.y
		space = _mask_space()
		_mode = 2
	else:
		Frame.materialize(layer, main.doc.parent_size(main.sel))
		space = _element_space()
		_mode = 1
		if Layer.is_group(layer):
			# 容器改尺寸会按比例改写子层的框，所以先拍一份快照，拖动时从快照重算
			# （否则每帧都在上一帧的结果上再乘一次，比例会滚雪球）。
			_start_children = (layer.get("children", []) as Array).duplicate(true)

	_handle = 0 if handle < 0 else handle
	_start_rect = space["rect"]
	_start_mouse = get_global_mouse_position()
	_pressed = true
	mouse_default_cursor_shape = Frame.cursor_for(_handle)
	queue_redraw()
	return true


func _drag(keep_ratio: bool) -> void:
	var layer := _layer()
	if layer.is_empty():
		return
	var space := _active_space()
	if space.is_empty():
		return
	var gdelta := get_global_mouse_position() - _start_mouse
	var delta := (gdelta / maxf(0.0001, float(space["scale"]))).rotated(
			-deg_to_rad(float(space["rotation"])))

	if _mode == 1:
		var t: Dictionary = layer["transform"]
		if _handle == 0:
			var moved := _start_rect.position + delta
			t["x"] = moved.x
			t["y"] = moved.y
		else:
			var nr := Frame.resized_rect(_start_rect, _handle, delta, keep_ratio)
			t["x"] = nr.position.x
			t["y"] = nr.position.y
			t["w"] = nr.size.x
			t["h"] = nr.size.y
			if Layer.is_group(layer):
				var sx := nr.size.x / maxf(1.0, _start_rect.size.x)
				var sy := nr.size.y / maxf(1.0, _start_rect.size.y)
				var anchor := Frame.anchor_point(_handle,
						Rect2(Vector2.ZERO, _start_rect.size))
				layer["children"] = Frame.scale_children(_start_children, sx, sy, anchor)
	elif _mode == 2:
		var mask: Dictionary = layer["mask"]
		if _handle == 0:
			var moved := _start_rect.position + delta
			mask["x"] = moved.x
			mask["y"] = moved.y
		else:
			var nr := Frame.resized_rect(_start_rect, _handle, delta, keep_ratio)
			mask["x"] = nr.position.x
			mask["y"] = nr.position.y
			mask["w"] = nr.size.x
			mask["h"] = nr.size.y

	main.apply_live()
	queue_redraw()


func _end_drag() -> void:
	_pressed = false
	_mode = 0
	_handle = -1
	_start_children = []
	main.apply_change(true)
