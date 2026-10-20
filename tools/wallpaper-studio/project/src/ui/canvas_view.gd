extends Control
## 画布：SubViewport 里跑 SceneBuilder.build(...)，整个容器按可用空间等比缩放。
##
## 预览走的就是导出用的那条构造路径（设计 §2.3 的不变式），所以画布上看到的
## 就是导出后桌面贴纸渲染出来的东西 —— 包括时钟脚本，它在这里也是真的在走。
##
## 交互（选中/拖动/缩放/遮罩手柄）全在 SelectionOverlay 里；这里只提供
## "屏幕坐标 <-> 文档坐标"的换算和轻量重绘入口。

const SceneBuilder = preload("res://src/build/scene_builder.gd")
const AnimationBuilder = preload("res://src/build/animation_builder.gd")
const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
const Timeline = preload("res://src/doc/timeline.gd")
const SelectionOverlay = preload("res://src/ui/selection_overlay.gd")

const PADDING := 28.0
## 静态画面重绘几帧后切成 UPDATE_ONCE：4K 画布常开每秒 8 兆像素没必要。
const WARMUP_FRAMES := 4
## 缩放范围：_zoom 是"相对适应窗口"的倍数，1 = 刚好放得下。
const MIN_ZOOM := 0.1
const MAX_ZOOM := 16.0
## 鼠标位置在缩放前后要落在同一个画布点上，判定阈值（像素）。
const ZOOM_STEP := 1.12

var main = null

## 播放头（秒）。预览里"拖时间轴"和"播放"改的都是它；重建预览后按它恢复画面。
var time := 0.0
var playing := false

var _bg: ColorRect
var _container: SubViewportContainer
var _viewport: SubViewport
var _frame: Panel
var _selection: Control
var _hint: Label
var _zoom_btn: Button
var _tools: HBoxContainer
var _warmup := 0
var _scale := 1.0
var _root: Control
## 用户缩放（1 = 适应窗口）与平移（屏幕像素偏移，_container 位置再叠加它）。
var _zoom := 1.0
var _pan := Vector2.ZERO
var _fit_scale := 1.0
var _panning := false
var _pan_mouse := Vector2.ZERO
var _pan_origin := Vector2.ZERO
## 预览树里的播放器（文档没有关键帧时为 null）。播放头由它评估，
## 不再用 GDScript 另算一遍曲线 —— 预览与导出只此一个求值器。
var _player: AnimationPlayer


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

	# 缩放工具条：右上角，和左下角的尺寸提示分开（那里文字会跟着画布尺寸变）。
	# 不用 set_anchors_preset：它按"调用那一刻"的 size 算偏移，按钮加进去之后
	# 宽度变了就会歪出去。锚点手动设，位置在 _apply_view 里按当前 size 重算。
	_tools = HBoxContainer.new()
	_tools.anchor_left = 1.0
	_tools.anchor_right = 1.0
	_tools.anchor_top = 0.0
	_tools.anchor_bottom = 0.0
	_tools.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	_tools.mouse_filter = Control.MOUSE_FILTER_PASS
	_tools.add_theme_constant_override("separation", 4)
	add_child(_tools)
	_zoom_btn = _tool_button(_tools, "100%", "点击回到「适应窗口」", func() -> void: fit_view())
	_tool_button(_tools, "＋", "放大（滚轮）", func() -> void: zoom_by(ZOOM_STEP))
	_tool_button(_tools, "－", "缩小（滚轮）", func() -> void: zoom_by(1.0 / ZOOM_STEP))

	resized.connect(_fit)
	_fit()


func _tool_button(parent: Control, text: String, tip: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.custom_minimum_size = Vector2(38, 24)
	b.add_theme_font_size_override("font_size", 12)
	b.pressed.connect(cb)
	parent.add_child(b)
	return b


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

	# 预览不装 autoplay：播放头由编辑器自己控制（拖时间轴 / 点播放）。
	# 节点结构仍然和导出完全一致（AnimationPlayer 也在，只是不自动播）。
	var root: Control = SceneBuilder.build(main.doc, main.project_dir, false)
	_viewport.add_child(root)
	_root = root
	_player = AnimationBuilder.player_of(root)
	apply_time()

	_warmup = WARMUP_FRAMES
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_fit()


## 把当前播放头应用到预览节点树。拖动时间轴、加/删关键帧、重建之后都走这里。
## 走的是预览树里的 AnimationPlayer，和导出后引擎跑的是同一个动画资源。
func apply_time() -> void:
	if _player == null:
		return
	if playing:
		if _player.current_animation != AnimationBuilder.ANIM_NAME:
			_player.play(AnimationBuilder.ANIM_NAME)
		_player.seek(time, true)
	else:
		AnimationBuilder.set_player_time(_player, time)


## 关键帧改了之后把新曲线装回播放器（不重建节点树），再按当前播放头落一次。
func recompile_animation() -> void:
	if _player == null or _root == null or main == null:
		return
	if AnimationBuilder.recompile(main.doc, _root, _player):
		apply_time()


## 跳到某个时刻。播放中跳转不打断播放（从新位置继续）。
func set_time(t: float) -> void:
	if main == null or main.doc == null:
		return
	time = clampf(t, 0.0, main.doc.timeline_end())
	apply_time()
	_warmup = WARMUP_FRAMES
	if _viewport != null:
		_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_update_selection()


func play() -> void:
	if main == null or main.doc == null:
		return
	# 只有"持续变化"的内容才值得开播放；纯静态壁纸点播放不该有任何反应。
	if not _is_dynamic() and not main.doc.has_animation():
		return
	if _player != null:
		_player.play(AnimationBuilder.ANIM_NAME)
		_player.seek(time, true)
	playing = true
	_warmup = WARMUP_FRAMES
	if _viewport != null:
		_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS


func pause() -> void:
	playing = false
	if _player != null:
		# 停在当前位置：读回播放器的时间，免得下一次 seek 把画面拉回旧值。
		time = _player.current_animation_position
		_player.pause()


func toggle_play() -> void:
	if playing:
		pause()
	else:
		play()


func _process(_delta: float) -> void:
	if playing and _player != null:
		# 播放头完全交给播放器：它自己按真实时间推进、按动画长度循环。
		time = _player.current_animation_position
		if _viewport != null and _viewport.render_target_update_mode != SubViewport.UPDATE_ALWAYS:
			_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		_warmup = 1
		_update_selection()
		return

	if _warmup <= 0:
		return
	_warmup -= 1
	if _warmup == 0 and not _is_dynamic() and _viewport != null:
		# 静态壁纸：关掉持续重绘。不缩略/缩放都不会让画面变糊 —— 缩放发生在
		# SubViewportContainer 的 Control 变换上，不改渲染分辨率。
		_viewport.render_target_update_mode = SubViewport.UPDATE_ONCE


## 有时钟/日期文字、粒子、动图时保持持续重绘，否则它们会停住。
## 关键帧不在这里：暂停时动画是静止的，只按播放头重绘一次就够了
## （播放中由 _process 强制 UPDATE_ALWAYS）。
func _is_dynamic() -> bool:
	if main == null or main.doc == null:
		return false
	if not main.doc.animated_image_layers().is_empty():
		return true
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
	# _fit_scale = "刚好放得下"的倍率；用户缩放 _zoom 乘在上面。
	_fit_scale = clampf(minf(avail.x / canvas.x, avail.y / canvas.y), 0.02, 1.0)
	_apply_view()


## 把 _zoom / _pan 落到 _container 上，并重画边框、选区、提示。
## 缩放只改 Control 的变换，不动 SubViewport 的渲染分辨率 —— 放大多看不糊。
func _apply_view() -> void:
	if main == null or main.doc == null or _container == null:
		return
	var canvas := Vector2(main.doc.canvas)
	var s := clampf(_fit_scale * _zoom, 0.02, MAX_ZOOM * 4.0)
	_scale = s
	_container.scale = Vector2(s, s)
	_container.position = (size - canvas * s) * 0.5 + _pan
	if _frame != null:
		_frame.position = _container.position
		_frame.size = canvas * s
	_update_selection()
	if _zoom_btn != null:
		_zoom_btn.text = "%d%%" % int(round(s * 100.0))
	if _tools != null:
		_tools.position = Vector2(size.x - _tools.size.x - 12.0, 10.0)
	if _hint != null:
		# 别把 % 操作符换到下一行：括号一闭合表达式就结束了，那行会变成新语句。
		var tip := "   ·   空白处拖动框选（Shift 加选）   ·   滚轮缩放、中键拖动画布"
		_hint.text = "%d × %d   ·   显示 %d%%   ·   拖动元素移动、拖手柄缩放（角点 Shift 等比）%s" % [
			int(canvas.x), int(canvas.y), int(round(s * 100.0)), tip]
		_hint.reset_size()
		_hint.position = Vector2(14.0, size.y - 24.0)


# ---------------------------------------------------------------- 视图：缩放 / 平移

## 缩放，并保持 anchor（屏幕坐标）下那个画布点不动 —— 滚轮缩放在光标处。
func zoom_by(factor: float, anchor: Vector2 = Vector2(-1.0, -1.0)) -> void:
	set_zoom(_zoom * factor, anchor)


func set_zoom(z: float, anchor: Vector2 = Vector2(-1.0, -1.0)) -> void:
	var next := clampf(z, MIN_ZOOM, MAX_ZOOM)
	if is_equal_approx(next, _zoom):
		return
	var a := anchor if anchor.x >= 0.0 else size * 0.5
	var before := to_canvas(a)          # 当前 scale 下的画布点
	_zoom = next
	_apply_view()
	var after := _container.position + before * _scale
	_pan += a - after                   # 把这个点挪回光标下面
	_apply_view()


func pan_by(delta: Vector2) -> void:
	_pan += delta
	_apply_view()


## 回到"适应窗口 + 居中"。
func fit_view() -> void:
	_zoom = 1.0
	_pan = Vector2.ZERO
	_apply_view()


func zoom_level() -> float:
	return _scale


func _gui_input(event: InputEvent) -> void:
	if main == null or main.doc == null:
		return
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		match mb.button_index:
			MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN:
				if not mb.pressed:
					return
				var factor := ZOOM_STEP if mb.button_index == MOUSE_BUTTON_WHEEL_UP \
						else 1.0 / ZOOM_STEP
				# 触控板滚轮会带 factor（一次事件滚很多），按它取幂。
				zoom_by(pow(factor, maxf(1.0, absf(mb.factor))), mb.position)
				accept_event()
				return
			MOUSE_BUTTON_MIDDLE:
				# 中键拖 = 平移（左键留给选中/拖动，Ctrl+左键也平移）。
				# 位置取自事件本身而不是 get_global_mouse_position()：后者来自 OS 光标，
				# 合成事件推不动它（带窗口的自动化走查就靠 push_input 发事件）。
				if mb.pressed:
					_panning = true
					_pan_mouse = _global_pos(mb.position)
					_pan_origin = _pan
				else:
					_panning = false
				accept_event()
				return
			MOUSE_BUTTON_LEFT:
				if mb.ctrl_pressed:
					if mb.pressed:
						_panning = true
						_pan_mouse = _global_pos(mb.position)
						_pan_origin = _pan
					else:
						_panning = false
					accept_event()
				return
		return
	if event is InputEventMouseMotion and _panning:
		_pan = _pan_origin + (_global_pos(event.position) - _pan_mouse)
		_apply_view()
		accept_event()


## 控件局部坐标 -> 全局（_gui_input 拿到的 position 是局部的）。
func _global_pos(local: Vector2) -> Vector2:
	return get_global_rect().position + local


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
	# 已经有关键帧的位置通道：拖拽就近写进播放头那一帧（自动关键帧），
	# 否则"拖了又被关键帧拉回去"看起来像拖不动。
	if _auto_key(layer):
		# 曲线变了，得先把新 key 装回播放器；否则 seek 的还是旧曲线。
		recompile_animation()
	else:
		apply_time()
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


## 自动关键帧：拖动画布上已经"有位置动画"的图层时，把当前值写进播放头那一帧。
## 只对她本来就有 track 的通道生效 —— 否则每拖一下都会凭空多出一条动画。
## 返回 true = 真的写了 key（调用方需要重编译播放器）。
func _auto_key(layer: Dictionary) -> bool:
	if main == null or main.doc == null:
		return false
	var tracks: Array = Timeline.tracks_of(layer)
	if tracks.is_empty():
		return false
	var wrote := false
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	var px := Vector2(layer.get("transform", {}).get("x", 0.0),
			layer.get("transform", {}).get("y", 0.0))
	for channel in ["x", "y"]:
		if Timeline.find_track(tracks, channel).is_empty():
			continue
		# 关键帧存父级比例，画布给的是像素 —— 在边界处换算一次。
		Timeline.set_key(layer, channel, time,
				Timeline.to_doc(channel, px.x if channel == "x" else px.y, parent_size))
		main.mark_dirty()
		wrote = true
	return wrote


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
		elif Timeline.has_animation(layer):
			# 有动画的图层：命中框按当前播放头的位置算，否则画面上看到的和点得到的错位。
			fr = Timeline.rect_at(layer, time, parent_size)
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
