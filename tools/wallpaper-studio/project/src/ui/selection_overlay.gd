extends Control
## 画布上的选区框：显示选中元素的位置与尺寸，拖动框内移动、拖手柄缩放。
##
## 用的都是 Godot 内建的东西：Control 的锚点/尺寸、rotation（绕 pivot_offset 旋转）、
## _gui_input、mouse_default_cursor_shape；边框就是内建的 ReferenceRect
## （它默认 editor_only = true，运行时不画，必须关掉）。
## 几何计算全部在 src/doc/frame.gd —— 纯函数、有无头用例钉住，这里只做坐标换算。
##
## 手感上的要点：拖动过程中**不能**用 event.position 算位移。控件自己的位置/尺寸
## 每帧都在变，局部坐标会跟着跳；统一用 get_global_mouse_position() 的差值。

const Frame = preload("res://src/doc/frame.gd")

const BORDER_COLOR := Color(0.42, 0.62, 0.95)
const HANDLE_SIZE := 9.0
## 控件框比可见边框大一圈：手柄有一半落在框外，不放大就点不到。
const PAD := HANDLE_SIZE * 0.5

var main = null
var display_scale := 1.0     # 屏幕像素 / 画布像素
var node_scale := 1.0        # transform.scale（历史文档里可能是 2）

var _border: ReferenceRect
var _pressed := false
var _handle := -1            # 0 = 拖位置；其余是 Frame 的手柄位掩码
var _start_mouse := Vector2.ZERO
var _start_rect := Rect2()
var _start_rotation := 0.0


func setup(m) -> void:
	main = m


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	mouse_default_cursor_shape = Control.CURSOR_MOVE

	_border = ReferenceRect.new()
	_border.editor_only = false
	_border.border_color = BORDER_COLOR
	_border.border_width = 1.0
	_border.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_border.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_border.offset_left = PAD
	_border.offset_top = PAD
	_border.offset_right = -PAD
	_border.offset_bottom = -PAD
	add_child(_border)

	resized.connect(queue_redraw)


func _handle_points() -> Array:
	var w := maxf(0.0, size.x - PAD * 2.0)
	var h := maxf(0.0, size.y - PAD * 2.0)
	# 顺序必须与 Frame.HANDLES 一致。
	return [
		Vector2(PAD, PAD), Vector2(PAD + w * 0.5, PAD), Vector2(PAD + w, PAD),
		Vector2(PAD + w, PAD + h * 0.5), Vector2(PAD + w, PAD + h),
		Vector2(PAD + w * 0.5, PAD + h), Vector2(PAD, PAD + h), Vector2(PAD, PAD + h * 0.5),
	]


func _draw() -> void:
	if size.x <= PAD * 2.0 or size.y <= PAD * 2.0:
		return
	var pts := _handle_points()
	for i in pts.size():
		var r := Rect2(pts[i] - Vector2(HANDLE_SIZE, HANDLE_SIZE) * 0.5,
				Vector2(HANDLE_SIZE, HANDLE_SIZE))
		draw_rect(r, Color(0.09, 0.1, 0.12, 0.92))
		draw_rect(r, BORDER_COLOR, false, 1.0)


## 命中哪个手柄；返回 -1 表示没命中（框内空白 = 拖位置）。
func _handle_at(p: Vector2) -> int:
	var pts := _handle_points()
	for i in pts.size():
		if p.distance_to(pts[i]) <= HANDLE_SIZE:
			return int(Frame.HANDLES[i])
	return -1


func _selected_layer() -> Dictionary:
	if main == null or main.doc == null:
		return {}
	if main.selected < 0 or main.selected >= main.doc.layers.size():
		return {}
	return main.doc.layers[main.selected]


func _gui_input(event: InputEvent) -> void:
	var layer := _selected_layer()
	if layer.is_empty():
		return
	var canvas: Vector2i = main.doc.canvas

	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if mb.pressed:
			_handle = _handle_at(mb.position)
			# 手柄之外一律当"拖位置"：点在框里就是移动，比要求必须点中细手柄好用。
			if _handle < 0:
				_handle = 0
			_start_drag(layer, canvas)
			accept_event()
		elif _pressed:
			_end_drag()
			accept_event()
		return

	if event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		if _pressed:
			_drag(layer, canvas, mm.shift_pressed)
			accept_event()
		else:
			var h := _handle_at(mm.position)
			mouse_default_cursor_shape = Frame.cursor_for(h) if h >= 0 else Control.CURSOR_MOVE


func _start_drag(layer: Dictionary, canvas: Vector2i) -> void:
	# 老图层（没定过尺寸）先落成显式框，之后拖的就是确定的数字。
	Frame.materialize(layer, canvas)
	_start_rect = Frame.of(layer, canvas)
	_start_mouse = get_global_mouse_position()
	_start_rotation = deg_to_rad(float(layer["transform"].get("rotation", 0.0)))
	_pressed = true
	mouse_default_cursor_shape = Frame.cursor_for(_handle)


func _drag(layer: Dictionary, canvas: Vector2i, keep_ratio: bool) -> void:
	var t: Dictionary = layer["transform"]
	var gdelta := get_global_mouse_position() - _start_mouse
	if _handle == 0:
		var moved := _start_rect.position + gdelta / maxf(0.0001, display_scale)
		t["x"] = moved.x
		t["y"] = moved.y
	else:
		# 屏幕位移 -> 元素本地像素：先反旋转，再除以显示缩放与节点缩放。
		var k := maxf(0.0001, display_scale * node_scale)
		var nr := Frame.resized_rect(_start_rect, _handle,
				gdelta.rotated(-_start_rotation) / k, keep_ratio)
		# 拉左边/上边时原点要跟着挪，否则对边会跟着跑。
		var origin_shift := (nr.position - _start_rect.position) * node_scale
		var origin := _start_rect.position + origin_shift.rotated(_start_rotation)
		t["x"] = origin.x
		t["y"] = origin.y
		t["w"] = nr.size.x
		t["h"] = nr.size.y
	main.apply_live()
	queue_redraw()


func _end_drag() -> void:
	_pressed = false
	_handle = -1
	main.apply_change(true)
