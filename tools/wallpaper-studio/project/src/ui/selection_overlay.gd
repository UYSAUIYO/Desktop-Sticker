extends Control
## 画布上的交互层：选区框、八个手柄、框选，以及鼠标拖动。
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
##
## 三种按下左键的后果，靠"有没有抓到框"和"有没有拖动"区分：
##   抓到框        -> 拖位置 / 缩放（多选时整组一起动）
##   空处 + 没动   -> 点选（Shift = 加选/反选，Alt = 选最外层容器）
##   空处 + 拖动   -> 框选（橡皮筋，Shift = 加到现有选择里）

const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")
const Timeline = preload("res://src/doc/timeline.gd")
const Motion = preload("res://src/doc/motion.gd")

const BORDER_COLOR := Color(0.42, 0.62, 0.95)
const MASK_COLOR := Color(0.96, 0.72, 0.24)
const REGION_COLOR := Color(0.36, 0.85, 0.62)
const EXTRA_COLOR := Color(0.55, 0.72, 0.95, 0.75)
const RUBBER_FILL := Color(0.36, 0.62, 0.95, 0.14)
const HANDLE_SIZE := 9.0
## 超过这个位移才算"拖"，否则算点击（手抖不该触发框选）。
const DRAG_SLOP := 4.0

var main = null
var canvas = null

var _brush := -1             # 0 = 拖位置；其余是 Frame 的手柄位掩码
var _pressed := false
var _start_mouse := Vector2.ZERO
var _start_rect := Rect2()
var _start_children := []
## 多选一起拖：除主选择外，每一层的起始位置与它所在空间的缩放/旋转。
var _start_multi: Array = []
## 框选：按下点在空白处，拖动超过 DRAG_SLOP 之后进入。
var _rubber := false
var _maybe_rubber := false
var _press_pos := Vector2.ZERO
var _rubber_to := Vector2.ZERO
var _rubber_add := false
## 拖动中的当前鼠标位置（全局）。**来自事件本身**，不读 get_global_mouse_position()：
## 后者是引擎从 OS 光标拿的，合成事件（自动化测试 push_input）推不动它，
## 那样一拖 delta 恒为 0、元素纹丝不动。真实鼠标下两者一致。
var _cur_mouse := Vector2.ZERO
## 正在拖的运动路径控制点（control_nodes 的下标），-1 = 没在拖。
var _motion_drag := -1


## 控件局部坐标 -> 全局。_gui_input 收到的 position 是局部的。
func _global_pos(local: Vector2) -> Vector2:
	return get_global_rect().position + local


func setup(m, c) -> void:
	main = m
	canvas = c


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	# PASS 而不是 STOP：本层铺满整个画布区，STOP 会**掐断事件向上传播**
	# （Viewport 里 MOUSE_FILTER_STOP 就是传播链的终点），那样中键拖动 / Ctrl+左键拖动
	# 永远到不了 CanvasView —— 平移整个功能是死的。左键相关的分支都会 accept_event()，
	# 所以正常使用不会漏到父级去。
	mouse_filter = Control.MOUSE_FILTER_PASS
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
	return _space_for(main.sel)


func _space_for(path: PackedInt32Array) -> Dictionary:
	if main == null or main.doc == null or path.is_empty():
		return {}
	var map := Frame.space_map(main.doc, path)
	var d: float = canvas.display_scale()
	return {
		"origin": canvas.container_origin() + (map["origin"] as Vector2) * d,
		"scale": float(map["scale"]) * d,
		"rotation": float(map["rotation"]),
		"rect": _rect_of(path),
	}


## 选区框 = 这一层在**当前播放头**的样子：有关键帧的图层跟着动画走，
## 否则框会停在基础位置，跟画面上的元素对不上。
## 祖先容器的动画暂不参与（框的空间仍按文档里的容器变换算）。
func _rect_of(path: PackedInt32Array) -> Rect2:
	# main / main.doc 是无类型引用（避免 preload 环），所以这里都要显式标注类型：
	# 用 := 会报 "cannot infer the type"。
	var layer: Dictionary = main.doc.layer_at(path)
	if layer.is_empty():
		return Rect2()
	var parent_size: Vector2 = main.doc.parent_size(path)
	if not Layer.is_effect(layer) and Timeline.has_animation(layer):
		return Timeline.rect_at(layer, canvas.time, parent_size)
	if not Layer.is_effect(layer) and Motion.enabled(layer):
		var p = Motion.sample_point(layer, canvas.time)
		if p != null:
			var size := Timeline.frame_size(layer, parent_size)
			return Rect2(Vector2(float(p.x) * parent_size.x,
					float(p.y) * parent_size.y), size)
	return main.doc.filter_frame(path)


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
	if main == null or main.doc == null:
		return
	# 附加选择先画（在下面），主选择的框画在最上面。
	for path in main.selected_paths():
		if path == main.sel:
			continue
		var space := _space_for(path)
		if space.is_empty():
			continue
		_draw_box(space, EXTRA_COLOR, false)
	var layer := _layer()
	if not _frameable(layer):
		return
	var space := _space()
	if space.is_empty():
		return
	# 滤镜层还没固定范围时画虚线：框是跟着被覆盖内容走的，不是它自己的固定框。
	var auto_follow := Layer.is_filter(layer) and not Filters.region_enabled(layer)
	_draw_box(space, _box_color(layer), true, auto_follow)
	_draw_motion(space, layer)
	if _rubber:
		_draw_rubber()


## 运动路径：曲线 + 端点 + 控制柄。只对**主选择**画（多选时附加层画会很乱）。
func _draw_motion(space: Dictionary, layer: Dictionary) -> void:
	if not Motion.enabled(layer):
		return
	var pts: Array = Motion.polyline(layer, main.doc.parent_size(main.sel), 48)
	if pts.size() < 2:
		return
	var screen_pts := []
	for p in pts:
		screen_pts.append(_to_screen(space, p))
	for i in screen_pts.size() - 1:
		draw_line(screen_pts[i], screen_pts[i + 1], Color(0.98, 0.62, 0.95), 1.5)
	var nodes: Array = Motion.control_nodes(layer, main.doc.parent_size(main.sel))
	for n in nodes:
		var pos := _to_screen(space, Vector2(float(n["x"]), float(n["y"])))
		var is_anchor := bool(n.get("anchor", false))
		var r := HANDLE_SIZE * (1.0 if is_anchor else 0.8)
		if is_anchor:
			draw_rect(Rect2(pos - Vector2(r, r) * 0.5, Vector2(r, r)),
					Color(0.98, 0.62, 0.95))
		else:
			draw_circle(pos, r * 0.5, Color(0.98, 0.62, 0.95, 0.85))
		if not is_anchor:
			var a: Vector2 = _to_screen(space, Vector2(float(n["ax"]), float(n["ay"])))
			draw_line(a, pos, Color(0.98, 0.62, 0.95, 0.6))


func _draw_rubber() -> void:
	var r := _rubber_rect()
	draw_rect(r, RUBBER_FILL)
	draw_rect(r, BORDER_COLOR, false, 1.0)


func _rubber_rect() -> Rect2:
	var a := _press_pos
	var b := _rubber_to
	return Rect2(Vector2(minf(a.x, b.x), minf(a.y, b.y)),
			Vector2(absf(b.x - a.x), absf(b.y - a.y)))


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
		if mb.ctrl_pressed:
			# Ctrl+左键 = 平移画布（交给 CanvasView）：这里是拖动元素的主要手势，
			# 两个都想要的话，元素就永远拖不动了。
			return
		if mb.pressed:
			_cur_mouse = _global_pos(mb.position)
			_press_pos = mb.position
			_rubber_to = mb.position
			_rubber_add = mb.shift_pressed
			if _try_start_drag(mb.position):
				_maybe_rubber = false
			else:
				_maybe_rubber = true
			accept_event()
		elif _pressed:
			_end_drag()
			accept_event()
		elif _rubber:
			_end_rubber()
			accept_event()
		elif _maybe_rubber:
			_end_click(mb.position, mb.shift_pressed, mb.alt_pressed)
			accept_event()
		return

	if event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		if _pressed:
			_cur_mouse = _global_pos(mm.position)
			_drag(mm.shift_pressed)
			accept_event()
			return
		if _maybe_rubber and mm.position.distance_to(_press_pos) > DRAG_SLOP:
			_maybe_rubber = false
			_rubber = true
		if _rubber:
			_rubber_to = mm.position
			queue_redraw()
			accept_event()
			return
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
	if _motion_handle_at(space, screen_p) >= 0:
		mouse_default_cursor_shape = Control.CURSOR_MOVE
		return
	var h := _handle_at(space, screen_p)
	if h >= 0:
		mouse_default_cursor_shape = Frame.cursor_for(h)
	elif _body_at(space, screen_p):
		mouse_default_cursor_shape = Control.CURSOR_MOVE
	else:
		mouse_default_cursor_shape = Control.CURSOR_ARROW


## 点空白处：改选中。Shift = 加选/反选；Alt = 选最外层容器。
func _end_click(screen_p: Vector2, shift: bool, alt: bool) -> void:
	_maybe_rubber = false
	var path: PackedInt32Array = canvas.outer_group_at_screen(screen_p) \
			if alt else canvas.layer_at_screen(screen_p)
	if shift:
		main.toggle_selection(path)
	else:
		main.select_layer_path(path)


func _end_rubber() -> void:
	_rubber = false
	var hits := _layers_in_rect(_rubber_rect())
	if hits.is_empty():
		# 空框 = 取消选择（除非是按 Shift 加选，那就不动现有选择）。
		if not _rubber_add:
			main.select_layer_path(PackedInt32Array())
		queue_redraw()
		return
	if _rubber_add:
		var paths: Array = main.selected_paths()
		for p in hits:
			if not paths.has(p):
				paths.append(p)
		main.set_selection(paths)
	else:
		main.set_selection(hits)
	queue_redraw()


## 与屏幕矩形相交的**内容图层**（滤镜/遮罩不参与：它们的框铺在内容上，
## 否则每次框选都会多抓一个效果层）。祖先也在集合里的子孙会被去掉。
func _layers_in_rect(rect: Rect2) -> Array:
	var out := []
	for item in main.doc.walk():
		var path: PackedInt32Array = item["path"]
		var layer: Dictionary = item["layer"]
		if Layer.is_effect(layer) or not bool(layer.get("visible", true)):
			continue
		var space := _space_for(path)
		if space.is_empty():
			continue
		var box := _screen_bounds(space)
		if not rect.intersects(box):
			continue
		out.append(path)
	# 只留最外层：父级被框进来时，它的子层不必再单独选中。
	var filtered := []
	for p in out:
		var covered := false
		for other in out:
			var op: PackedInt32Array = other
			if op.size() < p.size() and op == p.slice(0, op.size()):
				covered = true
				break
		if not covered:
			filtered.append(p)
	return filtered


func _screen_bounds(space: Dictionary) -> Rect2:
	var box := Rect2(_to_screen(space, _corners(space)[0]), Vector2.ZERO)
	for p in _corners(space):
		box = box.expand(_to_screen(space, p))
	return box


func _try_start_drag(screen_p: Vector2) -> bool:
	var layer := _layer()
	if not _frameable(layer):
		return false
	var space := _space()
	if space.is_empty():
		return false
	var motion_handle := _motion_handle_at(space, screen_p)
	if motion_handle >= 0:
		_begin_motion_drag(motion_handle)
		return true
	var handle := _handle_at(space, screen_p)
	if handle < 0 and not _body_at(space, screen_p):
		return false

	if Layer.is_filter(layer) and not Filters.region_enabled(layer):
		# 第一次碰滤镜层：先把框落成"现在被处理的那块"并固定范围 —— 拖的就是眼前
		# 这个框，位置不会跳。这里必须整棵重建（apply_change）：带模糊的滤镜只在
		# 构建时判定区域，要重建才会多出"框外显示原图"那层混合。
		main.doc.fix_filter_region(main.sel)
		main.apply_change(true, "固定滤镜范围")
		layer = _layer()
	else:
		Frame.materialize(layer, main.doc.parent_size(main.sel))
	space = _space()
	if Layer.is_group(layer):
		# 容器改尺寸会按比例改写子层的框，所以先拍一份快照，拖动时从快照重算
		# （否则每帧都在上一帧的结果上再乘一次，比例会滚雪球）。
		_start_children = (layer.get("children", []) as Array).duplicate(true)

	# 多选：记下其余各层的起始位置与所在空间，拖动时整组一起走。
	_start_multi = []
	for path in main.selected_paths():
		if path == main.sel:
			continue
		var other: Dictionary = main.doc.layer_at(path)
		if other.is_empty():
			continue
		var ospace := _space_for(path)
		if ospace.is_empty():
			continue
		_start_multi.append({
			"path": path,
			"x": float(other.get("transform", {}).get("x", 0.0)),
			"y": float(other.get("transform", {}).get("y", 0.0)),
			"scale": float(ospace["scale"]),
			"rotation": float(ospace["rotation"]),
		})

	_brush = 0 if handle < 0 else handle
	_start_rect = space["rect"]
	_start_mouse = _cur_mouse
	_pressed = true
	mouse_default_cursor_shape = Frame.cursor_for(_brush)
	main.break_history()
	queue_redraw()
	return true


func _drag(keep_ratio: bool) -> void:
	var layer := _layer()
	if layer.is_empty():
		return
	var space := _space()
	if space.is_empty():
		return
	var gdelta := _cur_mouse - _start_mouse
	var delta := (gdelta / maxf(0.0001, float(space["scale"]))).rotated(
			-deg_to_rad(float(space["rotation"])))

	if _motion_drag >= 0:
		_drag_motion(delta)
		return

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
	# 其余选中层：只跟着平移（缩放手柄只作用于主选择 —— 整组等比缩放另有入口）。
	for item in _start_multi:
		var other: Dictionary = main.doc.layer_at(item["path"])
		if other.is_empty():
			continue
		# delta 是在主选择的空间里算的，换到这一层自己的空间（父级缩放/旋转不同）。
		var s := float(item["scale"])
		var local := delta * (float(space["scale"]) / maxf(0.0001, s))
		local = local.rotated(deg_to_rad(float(space["rotation"]) - float(item["rotation"])))
		other["transform"]["x"] = float(item["x"]) + local.x
		other["transform"]["y"] = float(item["y"]) + local.y

	main.apply_live()
	queue_redraw()


func _end_drag() -> void:
	var was_motion := _motion_drag >= 0
	_pressed = false
	_brush = -1
	_motion_drag = -1
	_start_children = []
	_start_multi = []
	main.apply_change(true, "调整运动路径" if was_motion else "移动图层")


# ---------------------------------------------------------------- 运动路径的控制点

## 命中运动路径上的控制点（端点 + 控制柄），返回节点下标；没命中返回 -1。
func _motion_handle_at(space: Dictionary, screen_p: Vector2) -> int:
	var layer := _layer()
	if not Motion.enabled(layer):
		return -1
	var nodes: Array = Motion.control_nodes(layer, main.doc.parent_size(main.sel))
	var best := -1
	var best_d := HANDLE_SIZE
	for i in nodes.size():
		var n: Dictionary = nodes[i]
		var pos := _to_screen(space, Vector2(float(n["x"]), float(n["y"])))
		var d := pos.distance_to(screen_p)
		if d <= best_d:
			best_d = d
			best = i
	return best


func _begin_motion_drag(index: int) -> void:
	_motion_drag = index
	_start_mouse = get_global_mouse_position()
	_pressed = true
	main.break_history()
	queue_redraw()


## 拖动控制点：端点动位置；控制柄动曲率（对侧手柄跟着反向转，保持平滑）。
func _drag_motion(delta: Vector2) -> void:
	var layer := _layer()
	if layer.is_empty():
		return
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	Motion.move_node(layer, _motion_drag, Vector2(delta.x / maxf(1.0, parent_size.x),
			delta.y / maxf(1.0, parent_size.y)))
	main.apply_live()
	queue_redraw()
