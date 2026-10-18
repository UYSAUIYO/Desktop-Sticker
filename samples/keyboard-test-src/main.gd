extends Node3D
## 3D 键盘 / 鼠标输入测试台（.dswall 场景壁纸）。
##
## 用途：验证「空白桌面单击 → 焦点移交 → 低层钩子转发鼠标 + 原生键盘消息」这条链路。
## - 按键：单键、组合键（多键同时按住时每个键帽分别亮起），HUD 列出当前按住的全部键。
## - 鼠标：左 / 右 / 中键计数，滚轮计数，左键拖动（画轨迹并统计像素距离）。
## 纯程序化建模，不依赖任何贴图 / 字体资源，避免 template_release 运行库的资源导入限制。

const ROWS := ["1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"]
const SPACING := 1.0
const CAP_Y := 0.18
const MARKER_Y := 0.16   # 光点球心所在平面：球底刚好贴着桌面
const LINE_Y := 0.03     # 拖动轨迹线所在平面

var _caps: Array[Dictionary] = []
var _held: Dictionary = {}
var _down_count := 0
var _up_count := 0
var _key_events := 0
var _last_key := "（还没有按键）"

var _mouse_pos := Vector2.ZERO
var _dragging := false
var _drag_from := Vector2.ZERO
var _drag_total := 0.0
var _drag_last := 0.0
var _lmb := 0
var _rmb := 0
var _mmb := 0
var _wheel_up := 0
var _wheel_down := 0

var _marker: MeshInstance3D
var _drag_mesh: ImmediateMesh
var _drag_node: MeshInstance3D
var _hud_title: Label
var _hud_body: Label
var _dbg: FileAccess


func _ready() -> void:
	# 输入诊断：把引擎实际收到的每条事件写进 user://input.log（验证重复投递/坐标用）
	_dbg = FileAccess.open("user://input.log", FileAccess.WRITE)
	_build_environment()
	_build_desk()
	_build_keyboard()
	_build_marker()
	_build_hud()


func _exit_tree() -> void:
	if _dbg:
		_dbg.close()
		_dbg = null


func _build_environment() -> void:
	var we := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.027, 0.035, 0.052)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.6, 0.75)
	env.ambient_light_energy = 0.55
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	we.environment = env
	add_child(we)

	var cam := Camera3D.new()
	cam.fov = 46.0
	add_child(cam)
	cam.look_at_from_position(Vector3(0.0, 6.4, 8.6), Vector3(0.0, 0.5, 0.9), Vector3.UP)
	cam.current = true

	var key_light := DirectionalLight3D.new()
	key_light.light_energy = 1.15
	key_light.rotation_degrees = Vector3(-52.0, -28.0, 0.0)
	add_child(key_light)

	var fill := DirectionalLight3D.new()
	fill.light_energy = 0.35
	fill.light_color = Color(0.55, 0.7, 1.0)
	fill.rotation_degrees = Vector3(-20.0, 150.0, 0.0)
	add_child(fill)


func _build_desk() -> void:
	var mi := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(24.0, 14.0)
	mi.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.05, 0.06, 0.08)
	mat.roughness = 0.9
	mi.material_override = mat
	add_child(mi)


func _build_keyboard() -> void:
	var row_count := ROWS.size()
	for r in row_count:
		var row: String = ROWS[r]
		var z := (float(r) - (float(row_count) - 1.0) * 0.5) * SPACING
		for c in row.length():
			var ch := row[c]
			var x := (float(c) - (float(row.length()) - 1.0) * 0.5) * SPACING
			var code := OS.find_keycode_from_string(ch.to_upper())
			_add_cap(ch, code, Vector2(x, z), Vector2(0.86, 0.86))
	var space_z := (float(row_count) - 0.5) * SPACING
	_add_cap("SPACE", KEY_SPACE, Vector2(0.0, space_z), Vector2(6.4, 0.86))


func _add_cap(text: String, code: int, at: Vector2, size_2d: Vector2) -> void:
	var mi := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(size_2d.x, 0.16, size_2d.y)
	mi.mesh = box
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.12, 0.14, 0.19)
	mat.metallic = 0.25
	mat.roughness = 0.45
	mi.material_override = mat
	mi.position = Vector3(at.x, CAP_Y, at.y)
	add_child(mi)

	var lb := Label3D.new()
	lb.text = text
	lb.font_size = 56 if text.length() <= 2 else 34
	lb.pixel_size = 0.0062
	lb.modulate = Color(0.8, 0.86, 0.97)
	lb.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	lb.position = Vector3(0.0, 0.085, 0.0)
	mi.add_child(lb)

	_caps.append({"mesh": mi, "mat": mat, "code": code, "base_y": CAP_Y})


func _build_marker() -> void:
	_marker = MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 0.14
	sphere.height = 0.28
	_marker.mesh = sphere
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.35, 0.9, 1.0)
	mat.emission_enabled = true
	mat.emission = Color(0.15, 0.65, 0.95)
	mat.emission_energy_multiplier = 1.4
	_marker.material_override = mat
	_marker.position = Vector3(0.0, 0.16, 0.0)
	add_child(_marker)

	_drag_mesh = ImmediateMesh.new()
	_drag_node = MeshInstance3D.new()
	_drag_node.mesh = _drag_mesh
	var line_mat := StandardMaterial3D.new()
	line_mat.albedo_color = Color(1.0, 0.45, 0.35)
	line_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_drag_node.material_override = line_mat
	add_child(_drag_node)


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 5
	add_child(layer)

	var title := Label.new()
	title.add_theme_font_size_override("font_size", 27)
	title.add_theme_color_override("font_color", Color(0.95, 0.97, 1.0))
	title.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85))
	title.add_theme_constant_override("outline_size", 6)
	title.position = Vector2(28, 18)
	title.size = Vector2(1200, 40)
	layer.add_child(title)
	_hud_title = title

	var panel := ColorRect.new()
	panel.color = Color(0.02, 0.03, 0.05, 0.72)
	panel.position = Vector2(24, 62)
	panel.size = Vector2(1000, 240)
	layer.add_child(panel)

	var body := Label.new()
	body.add_theme_font_size_override("font_size", 20)
	body.add_theme_color_override("font_color", Color(0.86, 0.9, 0.96))
	body.position = Vector2(40, 74)
	body.size = Vector2(970, 216)
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	layer.add_child(body)
	_hud_body = body

	var hint := Label.new()
	hint.add_theme_font_size_override("font_size", 18)
	hint.add_theme_color_override("font_color", Color(0.8, 0.84, 0.9, 0.75))
	hint.position = Vector2(28, 1024)
	hint.size = Vector2(1300, 30)
	hint.text = "先在空白桌面单击一下（把键盘焦点交给壁纸），再按键盘 / 按住左键拖动 / 滚动滚轮。"
	layer.add_child(hint)


func _input(event: InputEvent) -> void:
	if _dbg:
		var pos := ""
		if event is InputEventMouse:
			var me := event as InputEventMouse
			pos = " pos=%d,%d" % [int(me.position.x), int(me.position.y)]
			if event is InputEventMouseMotion:
				var mm2 := event as InputEventMouseMotion
				pos += " rel=%d,%d" % [int(mm2.relative.x), int(mm2.relative.y)]
		_dbg.store_line("%d %s%s" % [Time.get_ticks_msec(), event.as_text(), pos])
	if event is InputEventKey:
		var key := event as InputEventKey
		_key_events += 1
		var code := key.physical_keycode if key.physical_keycode != 0 else key.keycode
		if code != 0:
			if key.pressed and not key.echo:
				_down_count += 1
				_held[int(code)] = true
			elif not key.pressed:
				_up_count += 1
				_held.erase(int(code))
			var key_name := OS.get_keycode_string(int(code))
			if key_name == "":
				key_name = "code=%d" % code
			_last_key = "%s（code=%d, phys=%d, uni=%d）" % [
				key_name, key.keycode, key.physical_keycode, key.unicode]
	elif event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		_mouse_pos = mb.position
		match mb.button_index:
			MOUSE_BUTTON_LEFT:
				if mb.pressed:
					_lmb += 1
					_dragging = true
					_drag_from = mb.position
					_drag_total = 0.0
				else:
					if _dragging:
						_drag_last = _drag_total
					_dragging = false
			MOUSE_BUTTON_RIGHT:
				if mb.pressed:
					_rmb += 1
			MOUSE_BUTTON_MIDDLE:
				if mb.pressed:
					_mmb += 1
			MOUSE_BUTTON_WHEEL_UP:
				_wheel_up += 1
			MOUSE_BUTTON_WHEEL_DOWN:
				_wheel_down += 1
	elif event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		_mouse_pos = mm.position
		if _dragging:
			_drag_total += mm.relative.length()


func _process(_delta: float) -> void:
	_update_caps()
	_update_marker()
	_update_drag_line()
	_update_hud()


func _update_caps() -> void:
	for cap in _caps:
		var code: int = cap["code"]
		var down: bool = Input.is_physical_key_pressed(code) or _held.has(code)
		var mat: StandardMaterial3D = cap["mat"]
		var mi: MeshInstance3D = cap["mesh"]
		var base_y := float(cap["base_y"])
		if down:
			mat.albedo_color = Color(0.16, 0.62, 0.85)
			mat.emission = Color(0.1, 0.55, 0.85)
			mat.emission_energy_multiplier = 1.1
			mi.position = Vector3(mi.position.x, base_y - 0.045, mi.position.z)
		else:
			mat.albedo_color = Color(0.12, 0.14, 0.19)
			mat.emission = Color(0.0, 0.0, 0.0)
			mat.emission_energy_multiplier = 0.0
			mi.position = Vector3(mi.position.x, base_y, mi.position.z)


func _update_marker() -> void:
	# 用相机射线把光标投到桌面上：这样光点屏幕上正好压在鼠标指针下面。
	# （早先按视口比例线性映射，透视投影下会越到边缘越偏，用户会看到"对不上"。）
	var p := _desk_point(_mouse_pos, MARKER_Y)
	_marker.position = Vector3(p.x, MARKER_Y, p.z)


func _update_drag_line() -> void:
	_drag_mesh.clear_surfaces()
	if not _dragging:
		return
	var a := _desk_point(_drag_from, LINE_Y)
	var b := _desk_point(_mouse_pos, LINE_Y)
	_drag_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	_drag_mesh.surface_add_vertex(Vector3(a.x, LINE_Y, a.z))
	_drag_mesh.surface_add_vertex(Vector3(b.x, LINE_Y, b.z))
	_drag_mesh.surface_end()


func _update_hud() -> void:
	var held_names := PackedStringArray()
	for code in _held.keys():
		var s := OS.get_keycode_string(int(code))
		held_names.append(s if s != "" else str(code))
	held_names.sort()

	var mouse_buttons := PackedStringArray()
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) or _dragging:
		mouse_buttons.append("左")
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		mouse_buttons.append("右")
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_MIDDLE):
		mouse_buttons.append("中")

	var vs := get_viewport().get_visible_rect().size
	_hud_title.text = "3D 键盘 / 鼠标测试台 v3 — 按键事件 %d 个，滚轮 %+d" % [
		_key_events, _wheel_up - _wheel_down]

	# 自检：光点投回屏幕后与光标的偏差。0 px = 光点正好压在鼠标指针下。
	var marker_dev := -1.0
	var cam := get_viewport().get_camera_3d()
	if cam:
		marker_dev = cam.unproject_position(_marker.position).distance_to(_mouse_pos)

	var held_text := ", ".join(held_names) if not held_names.is_empty() else "—"
	var buttons_text := ", ".join(mouse_buttons) if not mouse_buttons.is_empty() else "—"
	var lines := [
		"按住键：%s（共 %d 个）" % [held_text, held_names.size()],
		"按下 %d 次 · 松开 %d 次 · 最近：%s" % [_down_count, _up_count, _last_key],
		"鼠标位置：%d, %d（视口 %d x %d）· 当前按下：%s" % [
			int(_mouse_pos.x), int(_mouse_pos.y), int(vs.x), int(vs.y), buttons_text],
		"左键 %d 次 · 右键 %d 次 · 中键 %d 次 · 滚轮 上 %d / 下 %d" % [
			_lmb, _rmb, _mmb, _wheel_up, _wheel_down],
		"拖动：%s · 本次 %d px · 上次 %d px" % [
			"进行中" if _dragging else "未拖动", int(_drag_total), int(_drag_last)],
		"光点屏幕偏差：%d px（0 = 压在光标下）" % int(marker_dev),
	]
	_hud_body.text = "\n".join(PackedStringArray(lines))


## 光标客户区坐标 → 桌平面上与光标同屏的那一点。
## 优先走相机射线（透视正确，光点就压在鼠标指针下）；相机不可用时退回线性映射。
func _desk_point(pos: Vector2, plane_y: float) -> Vector3:
	var cam := get_viewport().get_camera_3d()
	if cam:
		var from := cam.project_ray_origin(pos)
		var dir := cam.project_ray_normal(pos)
		if absf(dir.y) > 0.00001:
			var t := (plane_y - from.y) / dir.y
			if t > 0.0:
				var hit := from + dir * t
				return Vector3(hit.x, plane_y, hit.z)
	return _screen_to_desk_legacy(pos, plane_y)


func _screen_to_desk_legacy(pos: Vector2, plane_y: float) -> Vector3:
	var vs := get_viewport().get_visible_rect().size
	if vs.x <= 1.0 or vs.y <= 1.0:
		vs = Vector2(1920.0, 1080.0)
	var nx := clampf(pos.x / vs.x, 0.0, 1.0)
	var ny := clampf(pos.y / vs.y, 0.0, 1.0)
	return Vector3(lerpf(-5.4, 5.4, nx), plane_y, lerpf(-3.6, 3.6, ny))
