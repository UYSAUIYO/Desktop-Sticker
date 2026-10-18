extends Control
## 画布：SubViewport 里跑 SceneBuilder.build(...)，整个容器按可用空间等比缩放。
##
## 预览走的就是导出用的那条构造路径（设计 §2.3 的不变式），所以画布上看到的
## 就是导出后桌面贴纸渲染出来的东西 —— 包括时钟脚本，它在这里也是真的在走。
##
## 交互（选中/拖动/缩放/遮罩手柄）全在 SelectionOverlay 里；这里只提供
## "屏幕坐标 <-> 文档坐标"的换算和轻量重绘入口。

const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
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
		_selection.setup(main, self)
	main.selection_changed.connect(func() -> void: _update_selection())


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

	# 选区框/遮罩手柄层铺满整个画布区（框和手柄都由它自己画），
	# 放在最上层，鼠标事件也由它统一处理。
	_selection = SelectionOverlay.new()
	_selection.visible = true    # 不画东西也保持可见：它同时接管画布上的点击
	if main != null:
		_selection.setup(main, self)
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

	# 先摘出树再 queue_free：直接 free 会在"某个子节点的信号正在发射"时 panic
	# （报 freed while a signal is being emitted）；remove_child 之后画面也不会闪。
	for c in _viewport.get_children():
		_viewport.remove_child(c)
		c.queue_free()
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
	for item in main.doc.walk():
		var t := String((item["layer"] as Dictionary).get("type", ""))
		if t == "particles":
			return true
		if t == "text" and String(item["layer"].get("dynamic", "none")) != "none":
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
		_hint.text = "%d × %d   ·   %d%%   ·   拖动元素移动、拖手柄缩放（角点 Shift 等比，Alt 点选容器）" % [
			int(canvas.x), int(canvas.y), int(round(s * 100.0))]
		_hint.reset_size()
		_hint.position = Vector2(14.0, size.y - 24.0)


# ---------------------------------------------------------------- 坐标换算

## 屏幕像素 / 画布像素。
func display_scale() -> float:
	return maxf(0.0001, _scale)


## 画布左上角在画布区里的位置。
func container_origin() -> Vector2:
	return _container.position


## 屏幕点 -> 画布像素（根空间）。
func to_canvas(screen_pos: Vector2) -> Vector2:
	return (screen_pos - _container.position) / display_scale()


# ---------------------------------------------------------------- 拖动中的轻量刷新

## 拖动过程中的轻量刷新：只改节点的位置/尺寸，不重建整棵树。
## 重建会让每个鼠标移动事件都重造一遍节点树，拖起来会一顿一顿的。
func apply_live(path: PackedInt32Array) -> void:
	if main == null or main.doc == null or _root == null:
		return
	var layer: Dictionary = main.doc.layer_at(path)
	if layer.is_empty():
		return
	_apply_live_subtree(_root, layer, main.doc.parent_size(path))
	# 效果层（遮罩）改的是着色器 uniform：只刷材质参数，不重建节点。
	SceneBuilder.refresh_effect(_root, main.doc, path, main.project_dir)
	# 静态画面平时是 UPDATE_ONCE（省电），拖动时必须重新开几帧重绘，
	# 否则预览停在原地、看起来像"拖不动"。
	_warmup = WARMUP_FRAMES
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_update_selection()


## 容器改尺寸时子层的框也变了，所以整棵子树都要同步（节点按图层 id 找）。
func _apply_live_subtree(base: Node, layer: Dictionary, parent_size: Vector2) -> void:
	var node := base.find_child(SceneBuilder.NODE_PREFIX + String(layer.get("id", "")),
			true, false) as Control
	if node == null:
		return
	SceneBuilder.apply_layer_node(node, layer, parent_size)
	if Layer.is_group(layer):
		var size := Frame.of(layer, parent_size).size
		var kids = layer.get("children")
		if kids is Array:
			for child in kids:
				if child is Dictionary:
					_apply_live_subtree(base, child, size)


func _update_selection() -> void:
	if _selection == null:
		return
	# 选区层必须**一直可见**：它同时负责画布上的点击（选中 / 取消选中）。
	# 曾经在"没选中"时把它藏掉，结果点空白取消选择之后再也点不回来了。
	_selection.visible = true
	_selection.queue_redraw()


## 选中变化时由 main 调用：重画选区框/手柄。
func refresh_selection() -> void:
	_update_selection()


# ---------------------------------------------------------------- 命中测试

## 画布上点哪一层就选哪一层（从最上层往下、递归进容器），空处返回空路径。
##
## 分两轮：先只找**内容**图层，找不到才轮到（没固定作用区域的）滤镜层。
## 滤镜层的框默认就铺在被处理的内容上，若它抢点击，底下所有东西都选不中了；
## 反过来完全不认它，用户又没法在画布上抓住它的框 —— 两轮正好兼顾。
## 选中滤镜层之后，拖它的框由选区层（SelectionOverlay）接管，不受这里影响。
func layer_at_screen(screen_pos: Vector2) -> PackedInt32Array:
	if main == null or main.doc == null:
		return PackedInt32Array()
	var canvas_point := to_canvas(screen_pos)
	var parent_size := Vector2(main.doc.canvas)
	var hit := _hit_children(main.doc.layers, PackedInt32Array(), canvas_point,
			parent_size, true)
	if hit.is_empty():
		hit = _hit_children(main.doc.layers, PackedInt32Array(), canvas_point,
				parent_size, false)
	return hit


func _hit_children(arr: Array, parent_path: PackedInt32Array, canvas_point: Vector2,
		parent_size: Vector2, skip_filters: bool) -> PackedInt32Array:
	for i in range(arr.size() - 1, -1, -1):
		var layer: Dictionary = arr[i]
		if not bool(layer.get("visible", true)):
			continue
		if skip_filters and Layer.is_filter(layer):
			continue
		var path := parent_path.duplicate()
		path.append(i)
		# 换算到本层 transform 所在空间（父空间）再判框。框在父空间里是轴对齐的：
		# 图层的旋转只转内容，不改框（渲染端也是这么画的）。
		var map := Frame.space_map(main.doc, path)
		var local := Frame.to_space(canvas_point, map)
		# 滤镜层的框：固定范围 = 自己的 transform，跟随内容 = 被覆盖内容的包围盒。
		var fr := Frame.of(layer, parent_size)
		if Layer.is_filter(layer):
			fr = main.doc.filter_frame(path)
		if not Frame.hit_rect(fr, local):
			continue
		if Layer.is_group(layer):
			var inner := _hit_children(layer.get("children", []), path, canvas_point,
					fr.size, skip_filters)
			if not inner.is_empty():
				return inner
		return path
	return PackedInt32Array()


## Alt+点击用：命中的那一层最外层容器（拿不到就返回空）。
func outer_group_at_screen(screen_pos: Vector2) -> PackedInt32Array:
	var hit := layer_at_screen(screen_pos)
	if hit.size() <= 1:
		return hit
	var out := PackedInt32Array()
	out.append(hit[0])
	return out
