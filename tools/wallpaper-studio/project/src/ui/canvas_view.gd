extends Control
## 画布：SubViewport 里跑 SceneBuilder.build(...)，整个容器按可用空间等比缩放。
##
## 预览走的就是导出用的那条构造路径（设计 §2.3 的不变式），所以画布上看到的
## 就是导出后桌面贴纸渲染出来的东西 —— 包括时钟脚本，它在这里也是真的在走。

const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Frame = preload("res://src/doc/frame.gd")
const SelectionOverlay = preload("res://src/ui/selection_overlay.gd")

const PADDING := 28.0
## 静态画面重绘几帧后切成 UPDATE_ONCE：4K 画布常开每秒 8 兆像素没必要。
const WARMUP_FRAMES := 4

var main = null

var _bg: ColorRect
var _container: SubViewportContainer
var _viewport: SubViewport
var _frame: Panel
var _selection: Control
var _hint: Label
var _warmup := 0
var _scale := 1.0
var _root: Control


func setup(m) -> void:
	main = m
	if _selection != null:
		_selection.setup(m)
	main.selection_changed.connect(func(_i: int) -> void: _update_selection())


func _ready() -> void:
	clip_contents = true
	mouse_filter = Control.MOUSE_FILTER_STOP

	_bg = ColorRect.new()
	_bg.color = Color(0.075, 0.078, 0.09)
	_bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_bg)

	_viewport = SubViewport.new()
	_viewport.transparent_bg = false
	_viewport.disable_3d = true
	_viewport.handle_input_locally = false
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS

	_container = SubViewportContainer.new()
	_container.stretch = false
	_container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_container.add_child(_viewport)
	add_child(_container)

	# 边框必须是**画布上面的兄弟节点**：Control 自己的 _draw 在子节点之前画，
	# 会被铺满的 _bg 直接盖掉（第一版就是这么错的）。
	_frame = Panel.new()
	_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var frame_style := StyleBoxFlat.new()
	frame_style.draw_center = false
	frame_style.border_width_left = 1
	frame_style.border_width_top = 1
	frame_style.border_width_right = 1
	frame_style.border_width_bottom = 1
	frame_style.border_color = Color(0.42, 0.58, 0.85, 0.85)
	frame_style.shadow_color = Color(0, 0, 0, 0.45)
	frame_style.shadow_size = 6
	_frame.add_theme_stylebox_override("panel", frame_style)
	add_child(_frame)

	# 选区框必须在 _frame 之上：它比画布边框更需要即时可见（拖动手柄时）。
	_selection = SelectionOverlay.new()
	_selection.visible = false
	if main != null:
		_selection.setup(main)
	add_child(_selection)

	_hint = Label.new()
	_hint.modulate = Color(1, 1, 1, 0.55)
	_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_hint.add_theme_font_size_override("font_size", 12)
	add_child(_hint)

	resized.connect(_fit)
	_fit()


## 用当前文档重建预览节点树。文档一变就调用（图层增删/改名/换色/换序都算）。
func rebuild() -> void:
	if main == null or main.doc == null or _viewport == null:
		return

	# 不要 queue_free：延迟释放会让新旧两棵树共存一帧，画面闪一下。
	for c in _viewport.get_children():
		_viewport.remove_child(c)
		c.free()
	_root = null

	var size_i: Vector2i = main.doc.canvas
	_viewport.size = size_i
	_container.size = Vector2(size_i)

	var root: Control = SceneBuilder.build(main.doc, main.project_dir)
	_viewport.add_child(root)
	_root = root

	_warmup = WARMUP_FRAMES
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_fit()


func _process(_delta: float) -> void:
	if _warmup <= 0:
		return
	_warmup -= 1
	if _warmup == 0 and not _is_dynamic() and _viewport != null:
		# 静态壁纸：关掉持续重绘。不缩略/缩放都不会让画面变糊 —— 缩放发生在
		# SubViewportContainer 的 Control 变换上，不改渲染分辨率。
		_viewport.render_target_update_mode = SubViewport.UPDATE_ONCE


## 有时钟/日期文字或粒子时保持持续重绘，否则它们会停住。
func _is_dynamic() -> bool:
	if main == null or main.doc == null:
		return false
	for l in main.doc.layers:
		var t := String(l.get("type", ""))
		if t == "particles":
			return true
		if t == "text" and String(l.get("dynamic", "none")) != "none":
			return true
	return false


func _fit() -> void:
	if main == null or main.doc == null or _container == null:
		return
	var canvas := Vector2(main.doc.canvas)
	var avail := size - Vector2(PADDING * 2.0, PADDING * 2.0)
	if avail.x <= 8.0 or avail.y <= 8.0:
		return
	var s := minf(avail.x / canvas.x, avail.y / canvas.y)
	s = clampf(s, 0.05, 1.0)
	_scale = s
	_container.scale = Vector2(s, s)
	_container.position = (size - canvas * s) * 0.5
	if _frame != null:
		_frame.position = _container.position
		_frame.size = canvas * s
	_update_selection()
	if _hint != null:
		_hint.text = "%d × %d   ·   %d%%   ·   拖动元素可移动，拖手柄缩放（角点按住 Shift 等比）" % [
			int(canvas.x), int(canvas.y), int(round(s * 100.0))]
		_hint.reset_size()
		_hint.position = Vector2(14.0, size.y - 24.0)


# ---------------------------------------------------------------- 选区框

## 拖动过程中的轻量刷新：只改节点的位置/尺寸，不重建整棵树。
## 重建会让每个鼠标移动事件都重造一遍节点树，拖起来会一顿一顿的。
func apply_live(index: int) -> void:
	if main == null or main.doc == null or _root == null:
		return
	if index < 0 or index >= main.doc.layers.size():
		return
	var node := _root.get_node_or_null("layer_%d" % index)
	if node is Control:
		SceneBuilder.apply_frame(node, main.doc.layers[index], main.doc.canvas)
	# 静态画面平时是 UPDATE_ONCE（省电），拖动时必须重新开几帧重绘，
	# 否则预览停在原地、看起来像"拖不动"。
	_warmup = WARMUP_FRAMES
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_update_selection()


## 把选中元素的框摆到它该在的屏幕位置（含旋转与节点缩放）。
func _update_selection() -> void:
	if _selection == null:
		return
	if main == null or main.doc == null or main.selected < 0 \
			or main.selected >= main.doc.layers.size():
		_selection.visible = false
		return
	var layer: Dictionary = main.doc.layers[main.selected]
	if not bool(layer.get("visible", true)):
		_selection.visible = false
		return
	var t: Dictionary = layer.get("transform", {})
	var sc := maxf(0.001, float(t.get("scale", 1.0)))
	var r := Frame.of(layer, main.doc.canvas)
	var pad := SelectionOverlay.PAD
	# 控件框 = 可见框 + PAD 一圈；pivot 放在可见框左上角，旋转才绕元素原点转。
	_selection.position = _container.position + r.position * _scale - Vector2(pad, pad)
	_selection.pivot_offset = Vector2(pad, pad)
	_selection.size = r.size * sc * _scale + Vector2(pad, pad) * 2.0
	_selection.rotation_degrees = float(t.get("rotation", 0.0))
	_selection.display_scale = _scale
	_selection.node_scale = sc
	_selection.visible = true
	_selection.queue_redraw()


## 画布上点哪一层就选哪一层（从最上层往下找），空处取消选择。
func _layer_at(screen_pos: Vector2) -> int:
	if main == null or main.doc == null:
		return -1
	var p := (screen_pos - _container.position) / maxf(0.0001, _scale)
	for i in range(main.doc.layers.size() - 1, -1, -1):
		var layer: Dictionary = main.doc.layers[i]
		if not bool(layer.get("visible", true)):
			continue
		var t: Dictionary = layer.get("transform", {})
		if Frame.hit_test(layer, main.doc.canvas, p,
				float(t.get("rotation", 0.0)), float(t.get("scale", 1.0))):
			return i
	return -1


func _gui_input(event: InputEvent) -> void:
	if main == null or main.doc == null:
		return
	if not (event is InputEventMouseButton):
		return
	var mb := event as InputEventMouseButton
	if mb.button_index != MOUSE_BUTTON_LEFT or not mb.pressed:
		return
	main.select_layer(_layer_at(mb.position))
	accept_event()
