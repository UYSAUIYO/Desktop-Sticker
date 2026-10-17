extends Control
## 画布：SubViewport 里跑 SceneBuilder.build(...)，整个容器按可用空间等比缩放。
##
## 预览走的就是导出用的那条构造路径（设计 §2.3 的不变式），所以画布上看到的
## 就是导出后桌面贴纸渲染出来的东西 —— 包括时钟脚本，它在这里也是真的在走。

const SceneBuilder = preload("res://src/build/scene_builder.gd")

const PADDING := 28.0
## 静态画面重绘几帧后切成 UPDATE_ONCE：4K 画布常开每秒 8 兆像素没必要。
const WARMUP_FRAMES := 4

var main = null

var _bg: ColorRect
var _container: SubViewportContainer
var _viewport: SubViewport
var _frame: Panel
var _hint: Label
var _warmup := 0


func setup(m) -> void:
	main = m


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

	var size_i: Vector2i = main.doc.canvas
	_viewport.size = size_i
	_container.size = Vector2(size_i)

	var root: Control = SceneBuilder.build(main.doc, main.project_dir)
	_viewport.add_child(root)

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
	_container.scale = Vector2(s, s)
	_container.position = (size - canvas * s) * 0.5
	if _frame != null:
		_frame.position = _container.position
		_frame.size = canvas * s
	if _hint != null:
		_hint.text = "%d × %d   ·   %d%%" % [int(canvas.x), int(canvas.y), int(round(s * 100.0))]
		_hint.reset_size()
		_hint.position = Vector2(14.0, size.y - 24.0)
