extends Control
## 3D 场景预览（mode = "3d" 时的"画布"）。
##
## 结构：一个 own_world_3d 的 SubViewport，里面放 SceneBuilder3D 构建的场景树。
## 相机有两台：
##   · _view_cam  —— 编辑视角（轨道相机：中键旋转、滚轮推拉、Shift+中键平移），
##     只存在于预览里，不会进导出包；
##   · 文档相机   —— SceneBuilder3D 建出来的 Camera，是**导出后用户看到的视角**。
## 「相机视角」按钮把预览切到文档相机 —— 与 Wallpaper Engine 的 Edit Camera POV 同一个
## 交互：在相机视角里挪动的就是文档相机本身。
##
## 播放：场景树里的 AnimationPlayer 是唯一求值器（与 2D 同一原则），这里只负责把
## time 塞给它（seek），不做第二条插值路径。

const SceneBuilder3D = preload("res://src/build/scene_builder_3d.gd")
const Scene3D = preload("res://src/doc/scene3d.gd")

var main = null
## 播放头（秒）。语义与 2D 画布一致：面板读它显示时间，set_time 拖它。
var time := 0.0
var playing := false
## true = 用文档相机看（所见即导出）；false = 编辑视角。
var camera_view := false

var _viewport: SubViewport
## SubViewport 不是 Control，直接 add_child 不会把渲染结果显示到界面上 ——
## 必须用 SubViewportContainer 承载它的纹理（stretch = 跟着容器尺寸走）。
var _viewport_box: SubViewportContainer
var _root: Node3D = null
var _doc_cam: Camera3D = null
var _view_cam: Camera3D
var _player: AnimationPlayer = null
var _hint: Label
var _bar: HBoxContainer
var _btn_play: Button
var _time_label: Label
var _btn_cam_view: Button
var _btn_key: Button
var _btn_del_key: Button
var _length: SpinBox

# 编辑轨道相机状态
var _pivot := Vector3(0.0, 0.2, 0.0)
var _dist := 7.0
var _yaw := 0.6
var _pitch := 0.35
var _nav := 0                     # 0 = 没按；1 = 旋转；2 = 平移
var _nav_mouse := Vector2.ZERO
var _nav_yaw := 0.0
var _nav_pitch := 0.0
var _nav_pivot := Vector3.ZERO


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	clip_contents = true

	_viewport = SubViewport.new()
	_viewport.own_world_3d = true
	_viewport.disable_3d = false
	_viewport.transparent_bg = false
	_viewport.handle_input_locally = false
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	# 鼠标事件由本控件统一处理（旋转 / 平移 / 拾取），容器别截走。
	_viewport_box = SubViewportContainer.new()
	_viewport_box.stretch = true
	_viewport_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_viewport_box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(_viewport_box)
	_viewport_box.add_child(_viewport)

	_view_cam = Camera3D.new()
	_view_cam.current = true
	_view_cam.fov = 55.0
	_viewport.add_child(_view_cam)
	_update_view_cam()

	# 底部工具条（时间 / 播放 / 视角切换 / 相机关键帧）。
	_bar = HBoxContainer.new()
	_bar.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	_bar.offset_top = -34.0
	_bar.offset_left = 8.0
	_bar.offset_right = -8.0
	_bar.offset_bottom = -6.0
	_bar.add_theme_constant_override("separation", 6)
	add_child(_bar)

	_btn_play = _tool_button(_bar, "播放", "播放 / 暂停（空格）")
	_btn_play.pressed.connect(func() -> void: toggle_play())
	_time_label = Label.new()
	_time_label.add_theme_font_size_override("font_size", 12)
	_bar.add_child(_time_label)
	_btn_cam_view = _tool_button(_bar, "编辑视角", "切换 编辑视角 / 文档相机视角")
	_btn_cam_view.pressed.connect(toggle_camera_view)
	var lbl := Label.new()
	lbl.text = "时长"
	lbl.add_theme_font_size_override("font_size", 12)
	_bar.add_child(lbl)
	_length = SpinBox.new()
	_length.min_value = 0.25
	_length.max_value = 600.0
	_length.step = 0.25
	_length.suffix = "s"
	_length.custom_minimum_size = Vector2(78, 0)
	_length.value_changed.connect(func(v: float) -> void:
		if main != null and main.doc != null:
			main.doc.loop_seconds = v
			main.apply_change(true, "时长"))
	_bar.add_child(_length)
	_btn_key = _tool_button(_bar, "写相机关键帧", "把当前机位记到播放头处（相机视角下）")
	_btn_key.pressed.connect(_write_camera_key)
	_btn_key.disabled = true
	_btn_del_key = _tool_button(_bar, "删关键帧", "删掉播放头处的相机关键帧（相机视角下）")
	_btn_del_key.pressed.connect(_remove_camera_key)
	_btn_del_key.disabled = true

	_hint = Label.new()
	_hint.modulate = Color(1, 1, 1, 0.55)
	_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_hint.add_theme_font_size_override("font_size", 12)
	_hint.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE)
	_hint.offset_left = 12.0
	_hint.offset_top = 6.0
	_hint.offset_right = -12.0
	_hint.text = "3D ·  中键旋转视角 · Shift+中键平移 · 滚轮推拉 · 点击物体选中"
	add_child(_hint)

	resized.connect(func() -> void: _viewport.size = Vector2i(size))


func setup(m) -> void:
	main = m


static func _tool_button(parent: Control, text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 12)
	parent.add_child(b)
	return b


# ---------------------------------------------------------------- 构建 / 时间

func rebuild() -> void:
	if main == null or main.doc == null or _viewport == null:
		return
	if _root != null:
		_viewport.remove_child(_root)
		_root.free()
		_root = null
	var root = SceneBuilder3D.build(main.doc, main.project_dir, false)
	_root = root
	_viewport.add_child(root)
	_doc_cam = root.get_node_or_null("Camera") as Camera3D
	_player = root.get_node_or_null("Player") as AnimationPlayer
	if _doc_cam != null:
		_doc_cam.current = camera_view
	_view_cam.current = not camera_view
	_apply_time()
	_length.set_value_no_signal(maxf(0.25, main.doc.loop_seconds))


## 与 2D 画布同名：改了文档后整体重建（3D 场景没有"只改一个节点"的轻量路径，
## 场景树很小，重建便宜）。
func recompile_animation() -> void:
	rebuild()


## 拖动过程中的轻量刷新 —— 3D 没有，直接重建。
func apply_live(_path = null) -> void:
	rebuild()


func refresh_selection() -> void:
	pass          # 选中态由 Inspector3D / ObjectsPanel 表达，3D 视口里不画框


func set_time(t: float) -> void:
	time = clampf(t, 0.0, _end())
	_apply_time()
	_sync_readout()


func toggle_play() -> void:
	if playing:
		pause()
	else:
		play()


func play() -> void:
	if _player == null:
		return
	playing = true
	if time >= _end() - 0.0001:
		time = 0.0


func pause() -> void:
	playing = false
	_sync_readout()


func _end() -> float:
	return 0.25 if main == null or main.doc == null else main.doc.timeline_end()


func _apply_time() -> void:
	SceneBuilder3D.set_player_time(_player, time)


func _process(delta: float) -> void:
	if playing and _player != null:
		time += delta
		if time >= _end():
			time = fposmod(time, _end())
		_apply_time()
		_sync_readout()
	if _nav != 0:
		_update_view_cam()


func _sync_readout() -> void:
	_time_label.text = "%.2f / %.2f s" % [time, _end()]
	_btn_play.text = "暂停" if playing else "播放"


# ---------------------------------------------------------------- 相机关键帧

func toggle_camera_view() -> void:
	camera_view = not camera_view
	if _doc_cam != null:
		_doc_cam.current = camera_view
	_view_cam.current = not camera_view
	_btn_cam_view.text = "相机视角" if camera_view else "编辑视角"
	# 相机视角下点「写关键帧」才有意义：把按钮的可用状态跟视角绑定。
	_btn_key.disabled = not camera_view
	_btn_del_key.disabled = not camera_view


func _write_camera_key() -> void:
	if main == null or main.doc == null or not main.doc.is_3d():
		return
	if not camera_view:
		main.show_message("提示", "先切到「相机视角」再写关键帧。")
		return
	Scene3D.set_camera_key(main.doc.camera3d, time)
	main.apply_change(true, "写相机关键帧")


func _remove_camera_key() -> void:
	if main == null or main.doc == null:
		return
	Scene3D.remove_camera_key(main.doc.camera3d, time)
	main.apply_change(true, "删除相机关键帧")


## 编辑视角 / 相机视角下拖动文档相机（相机视角下的 WASD 漫游没有做：
## 壁纸编辑里把数值填准比自由漫游更常用，漫游以后按需求再加）。
func move_doc_camera(delta_pos: Vector3, delta_target: Vector3, delta_fov: float) -> void:
	if main == null or main.doc == null:
		return
	var cam: Dictionary = main.doc.camera3d
	var pos := Scene3D.vec3(cam["pos"]) + delta_pos
	var target := Scene3D.vec3(cam["target"]) + delta_target
	cam["pos"] = [pos.x, pos.y, pos.z]
	cam["target"] = [target.x, target.y, target.z]
	cam["fov"] = clampf(float(cam.get("fov", 55.0)) + delta_fov, 10.0, 150.0)
	if _doc_cam != null:
		_doc_cam.position = pos
		_doc_cam.fov = float(cam["fov"])
		_aim(_doc_cam, target)
	rebuild()


# ---------------------------------------------------------------- 视角导航

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		match mb.button_index:
			MOUSE_BUTTON_MIDDLE:
				_nav = 2 if mb.shift_pressed else 1
				_nav_mouse = mb.position
				_nav_yaw = _yaw
				_nav_pitch = _pitch
				_nav_pivot = _pivot
				accept_event()
			MOUSE_BUTTON_WHEEL_UP:
				if mb.pressed:
					_dist = clampf(_dist * 0.9, 0.5, 200.0)
					_update_view_cam()
					accept_event()
			MOUSE_BUTTON_WHEEL_DOWN:
				if mb.pressed:
					_dist = clampf(_dist * 1.1, 0.5, 200.0)
					_update_view_cam()
					accept_event()
			MOUSE_BUTTON_LEFT:
				if mb.pressed:
					_pick(mb.position)
		return
	if event is InputEventMouseMotion and _nav != 0:
		var mm := event as InputEventMouseMotion
		var d := mm.position - _nav_mouse
		if _nav == 1:
			_yaw = _nav_yaw - d.x * 0.01
			_pitch = clampf(_nav_pitch + d.y * 0.01, -1.5, 1.5)
		else:
			var right := _view_cam.global_transform.basis.x
			var up := _view_cam.global_transform.basis.y
			_pivot = _nav_pivot - right * d.x * _dist * 0.0015 + up * d.y * _dist * 0.0015
		_update_view_cam()
		accept_event()


func _update_view_cam() -> void:
	var cp := cos(_pitch)
	_view_cam.position = _pivot + Vector3(sin(_yaw) * cp, sin(_pitch), cos(_yaw) * cp) * _dist
	_aim(_view_cam, _pivot)


static func _aim(node: Node3D, target: Vector3) -> void:
	var dir := target - node.position
	if dir.length() < 0.0001:
		return
	if absf(dir.normalized().dot(Vector3.UP)) > 0.999:
		dir += Vector3(0.001, 0.0, 0.001)
	node.look_at_from_position(node.position, target, Vector3.UP)


# ---------------------------------------------------------------- 拾取

## 屏幕空间拾取：把每个物体的原点投到屏幕上，取点击点最近且在 24px 内的。
## 不走物理射线（网格默认没有碰撞体，为拾取建一套 CollisionShape 不划算）。
func _pick(screen_p: Vector2) -> void:
	if main == null or main.doc == null or _viewport == null:
		return
	var cam := _viewport.get_camera_3d()
	if cam == null:
		return
	var best := -1
	var best_d := 24.0
	for i in main.doc.objects3d.size():
		var o: Dictionary = main.doc.objects3d[i]
		if not (o is Dictionary) or not bool(o.get("visible", true)):
			continue
		if not Scene3D.is_content(o):
			continue
		var world := Scene3D.vec3(o.get("pos", [0.0, 0.0, 0.0]))
		if cam.is_position_behind(world):
			continue
		var sp := cam.unproject_position(world)
		var d := sp.distance_to(screen_p)
		if d < best_d:
			best_d = d
			best = i
	if best < 0:
		main.select_layer_path(PackedInt32Array())
		return
	main.select_layer_path(PackedInt32Array([best]))
