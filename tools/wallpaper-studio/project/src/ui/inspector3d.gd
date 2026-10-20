extends Control
## 3D 属性面板（mode = "3d"）。选中相机（sel = [-1]）时编辑机位与路径关键帧，
## 选中物体时编辑变换 / 材质 / 类型参数 / 参数化动画。
##
## 与 2D Inspector 的分工原因：字段集合完全不同（Vector3 vs 父级比例 + 父级像素），
## 硬塞进同一个类会用 if mode == "3d" 把每个方法劈成两半，可读性反而差。

const Scene3D = preload("res://src/doc/scene3d.gd")
const ObjectsPanel = preload("res://src/ui/objects_panel.gd")

var main = null
var _scroll: ScrollContainer
var _box: VBoxContainer
var _syncing := false


func _ready() -> void:
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 10)
	margin.add_theme_constant_override("margin_right", 10)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	add_child(margin)
	_scroll = ScrollContainer.new()
	_scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	margin.add_child(_scroll)
	_box = VBoxContainer.new()
	_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_box.add_theme_constant_override("separation", 6)
	_scroll.add_child(_box)


func setup(m) -> void:
	main = m


func refresh(rebuild: bool = true) -> void:
	if not rebuild or main == null or main.doc == null or _box == null:
		return
	_syncing = true
	for c in _box.get_children():
		c.free()
	if main.sel.is_empty():
		_section("3D 场景")
		_note("没有选中对象。左侧列表里点一个，或在预览里点击物体。")
		_section("环境")
		_env_controls()
		_syncing = false
		return
	if main.sel.size() == 1 and int(main.sel[0]) == ObjectsPanel.CAMERA_PATH:
		_camera_section()
	else:
		var i := int(main.sel[0])
		if i >= 0 and i < main.doc.objects3d.size():
			_object_section(main.doc.objects3d[i])
		else:
			_note("选中的对象已经不存在了。")
	_syncing = false


## 播放头变了（拖预览的播放 / 关键帧）。3D 面板没有跟着走的数值，空实现。
func sync_time() -> void:
	pass


# ---------------------------------------------------------------- 相机

func _camera_section() -> void:
	var cam: Dictionary = main.doc.camera3d
	_section("相机")
	_vec3_row("机位", Scene3D.vec3(cam["pos"]), func(v: Vector3) -> void:
		cam["pos"] = [v.x, v.y, v.z]
		main.apply_change(false, "相机位置"), "米")
	_vec3_row("看向", Scene3D.vec3(cam["target"]), func(v: Vector3) -> void:
		cam["target"] = [v.x, v.y, v.z]
		main.apply_change(false, "相机看向"), "米")
	_spin_row("视野（FOV）", float(cam.get("fov", 55.0)), 10.0, 150.0, 1.0, "",
			func(v: float) -> void:
				cam["fov"] = v
				main.apply_change(false, "相机视野"))
	var modes := OptionButton.new()
	for m in ["循环 loop", "往返 mirror", "单次 once"]:
		modes.add_item(m)
	modes.select(clampi(["loop", "mirror", "once"].find(String(cam.get("play", "loop"))), 0, 2))
	modes.item_selected.connect(func(idx: int) -> void:
		cam["play"] = ["loop", "mirror", "once"][idx]
		main.apply_change(false, "路径播放模式"))
	_row("路径播放", modes)
	_note("写关键帧：预览切到「相机视角」，把机位摆好，点「写相机关键帧」。")

	_section("路径关键帧（%d）" % (cam.get("keys", []) as Array).size())
	var keys: Array = cam.get("keys", [])
	if keys.is_empty():
		_note("还没有关键帧 —— 相机固定不动。")
	for i in keys.size():
		var k: Dictionary = keys[i]
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 4)
		var lbl := Label.new()
		lbl.text = "#%d  %.2fs" % [i + 1, float(k["t"])]
		lbl.add_theme_font_size_override("font_size", 12)
		lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(lbl)
		var del := Button.new()
		del.text = "删除"
		del.focus_mode = Control.FOCUS_NONE
		del.add_theme_font_size_override("font_size", 12)
		del.pressed.connect(func() -> void:
			Scene3D.remove_camera_key(main.doc.camera3d, float(k["t"]))
			main.apply_change(true, "删除相机关键帧"))
		line.add_child(del)
		_box.add_child(line)


# ---------------------------------------------------------------- 物体

func _object_section(o: Dictionary) -> void:
	var type := String(o.get("type", "box"))
	_section(String(ObjectsPanel.TYPE_NAMES.get(type, type)))
	var name_edit := LineEdit.new()
	name_edit.text = String(o.get("name", ""))
	name_edit.text_changed.connect(func(t: String) -> void:
		o["name"] = t
		main.apply_change(false, "重命名"))
	_row("名称", name_edit)
	_vec3_row("位置", Scene3D.vec3(o["pos"]), func(v: Vector3) -> void:
		o["pos"] = [v.x, v.y, v.z]
		main.apply_change(false, "移动"), "米")
	_vec3_row("旋转", Scene3D.vec3(o["rot"]), func(v: Vector3) -> void:
		o["rot"] = [v.x, v.y, v.z]
		main.apply_change(false, "旋转"), "度")
	_vec3_row("缩放", Scene3D.vec3(o["scale"]), func(v: Vector3) -> void:
		o["scale"] = [v.x, v.y, v.z]
		main.apply_change(false, "缩放"), "")

	_section("参数")
	_type_params(o, type)

	if not (type in ["text", "light_sun", "light_omni", "model"]):
		_section("材质")
		var mat: Dictionary = o.get("material", {})
		_color_row("颜色", String(mat.get("albedo", "#c8ccd4")), func(c: String) -> void:
			mat["albedo"] = c
			main.apply_change(false, "颜色"))
		_spin_row("金属度", float(mat.get("metallic", 0.0)), 0.0, 1.0, 0.01, "",
				func(v: float) -> void:
					mat["metallic"] = v
					main.apply_change(false, "金属度"))
		_spin_row("粗糙度", float(mat.get("roughness", 0.55)), 0.0, 1.0, 0.01, "",
				func(v: float) -> void:
					mat["roughness"] = v
					main.apply_change(false, "粗糙度"))
		_color_row("自发光", String(mat.get("emission", "#000000")), func(c: String) -> void:
			mat["emission"] = c
			main.apply_change(false, "自发光"))
		_spin_row("发光强度", float(mat.get("emission_energy", 1.0)), 0.0, 16.0, 0.1, "",
				func(v: float) -> void:
					mat["emission_energy"] = v
					main.apply_change(false, "发光强度"))
		_spin_row("不透明度", float(mat.get("alpha", 1.0)), 0.0, 1.0, 0.01, "",
				func(v: float) -> void:
					mat["alpha"] = v
					main.apply_change(false, "不透明度"))

	if type != "text" and type != "model" and not Scene3D.is_light(o):
		_section("动画（循环）")
		var anim: Dictionary = o.get("anim", {})
		_vec3_row("自转（度/秒）", Scene3D.vec3(anim.get("spin", [0.0, 0.0, 0.0])),
				func(v: Vector3) -> void:
					anim["spin"] = [v.x, v.y, v.z]
					main.apply_change(false, "自转"), "")
		_spin_row("浮动幅度", float(anim.get("bob", 0.0)), 0.0, 10.0, 0.05, "米",
				func(v: float) -> void:
					anim["bob"] = v
					main.apply_change(false, "浮动"))
		_spin_row("浮动速度", float(anim.get("bob_speed", 1.0)), 0.0, 8.0, 0.05, "",
				func(v: float) -> void:
					anim["bob_speed"] = v
					main.apply_change(false, "浮动速度"))
		var orbit: Dictionary = anim.get("orbit", {})
		_spin_row("环绕半径", float(orbit.get("radius", 0.0)), 0.0, 50.0, 0.1, "米",
				func(v: float) -> void:
					orbit["radius"] = v
					main.apply_change(false, "环绕"))
		_spin_row("环绕速度", float(orbit.get("speed", 0.5)), 0.0, 4.0, 0.01, "圈/秒",
				func(v: float) -> void:
					orbit["speed"] = v
					main.apply_change(false, "环绕"))


func _type_params(o: Dictionary, type: String) -> void:
	var p: Dictionary = o.get("params", {})
	match type:
		"box":
			_vec3_row("尺寸", Scene3D.vec3(p.get("size", [1.0, 1.0, 1.0])),
					func(v: Vector3) -> void:
						p["size"] = [v.x, v.y, v.z]
						main.apply_change(false, "尺寸"), "米")
		"sphere":
			_spin_row("半径", float(p.get("radius", 0.6)), 0.01, 20.0, 0.01, "米",
					func(v: float) -> void:
						p["radius"] = v
						main.apply_change(false, "半径"))
		"cylinder":
			_spin_row("半径", float(p.get("radius", 0.5)), 0.01, 20.0, 0.01, "米",
					func(v: float) -> void:
						p["radius"] = v
						main.apply_change(false, "半径"))
			_spin_row("高", float(p.get("height", 1.2)), 0.01, 40.0, 0.01, "米",
					func(v: float) -> void:
						p["height"] = v
						main.apply_change(false, "高度"))
		"torus":
			_spin_row("内径", float(p.get("inner", 0.35)), 0.01, 20.0, 0.01, "米",
					func(v: float) -> void:
						p["inner"] = v
						main.apply_change(false, "内径"))
			_spin_row("外径", float(p.get("outer", 0.8)), 0.02, 40.0, 0.01, "米",
					func(v: float) -> void:
						p["outer"] = v
						main.apply_change(false, "外径"))
		"plane":
			_vec3_row("尺寸", Scene3D.vec3(p.get("size", [12.0, 12.0, 0.0])),
					func(v: Vector3) -> void:
						p["size"] = [v.x, v.y, 0.0]
						main.apply_change(false, "尺寸"), "米")
		"text":
			var edit := LineEdit.new()
			edit.text = String(p.get("text", ""))
			edit.text_changed.connect(func(t: String) -> void:
				p["text"] = t
				main.apply_change(false, "文字"))
			_row("内容", edit)
			_spin_row("字号", float(p.get("font_size", 96)), 8, 512, 1, "",
					func(v: float) -> void:
						p["font_size"] = int(v)
						main.apply_change(false, "字号"))
			_color_row("颜色", String(p.get("color", "#ffffff")), func(c: String) -> void:
				p["color"] = c
				main.apply_change(false, "文字颜色"))
			_check_row("始终朝向相机", bool(p.get("billboard", true)), func(on: bool) -> void:
				p["billboard"] = on
				main.apply_change(false, "朝向"))
		"light_sun":
			_light_params(p)
			_spin_row("光斑角", float(p.get("angle", 30.0)), 0.0, 89.0, 1.0, "度",
					func(v: float) -> void:
						p["angle"] = v
						main.apply_change(false, "光斑角"))
		"light_omni":
			_light_params(p)
			_spin_row("照射距离", float(p.get("range", 8.0)), 0.1, 100.0, 0.1, "米",
					func(v: float) -> void:
						p["range"] = v
						main.apply_change(false, "照射距离"))
		"model":
			var lbl := Label.new()
			lbl.text = String(p.get("src", "（未选择）"))
			lbl.add_theme_font_size_override("font_size", 12)
			_row("文件", lbl)
			_note("模型在左侧「＋ 添加 → 模型」里导入；导入时统一缩放到 1 米高"
					+ "（缩放倍数 × 物体的「缩放」）。")


func _light_params(p: Dictionary) -> void:
	_spin_row("强度", float(p.get("energy", 1.0)), 0.0, 16.0, 0.05, "",
			func(v: float) -> void:
				p["energy"] = v
				main.apply_change(false, "光强"))
	_color_row("颜色", String(p.get("color", "#ffffff")), func(c: String) -> void:
		p["color"] = c
		main.apply_change(false, "光色"))


# ---------------------------------------------------------------- 环境

func _env_controls() -> void:
	var env: Dictionary = main.doc.env3d
	_color_row("天空（上）", String(env.get("sky_top", "#0a1428")), func(c: String) -> void:
		env["sky_top"] = c
		main.apply_change(false, "天空"))
	_color_row("天空（下）", String(env.get("sky_bottom", "#1c2c44")), func(c: String) -> void:
		env["sky_bottom"] = c
		main.apply_change(false, "天空"))
	_spin_row("天空亮度", float(env.get("sky_energy", 1.0)), 0.0, 8.0, 0.05, "",
			func(v: float) -> void:
				env["sky_energy"] = v
				main.apply_change(false, "天空亮度"))
	_spin_row("环境光", float(env.get("ambient", 0.35)), 0.0, 4.0, 0.05, "",
			func(v: float) -> void:
				env["ambient"] = v
				main.apply_change(false, "环境光"))
	_check_row("辉光", bool(env.get("glow", true)), func(on: bool) -> void:
		env["glow"] = on
		main.apply_change(false, "辉光"))
	_check_row("雾", bool(env.get("fog", false)), func(on: bool) -> void:
		env["fog"] = on
		main.apply_change(false, "雾"))


# ---------------------------------------------------------------- 控件工厂

## 一行 [标签 : 控件]。返回控件。
func _row(label: String, control: Control) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(88, 0)
	l.add_theme_font_size_override("font_size", 12)
	line.add_child(l)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.add_child(control)
	_box.add_child(line)
	return control


func _spin_row(label: String, value: float, minv: float, maxv: float, step: float,
		suffix: String, on_change: Callable) -> SpinBox:
	var s := SpinBox.new()
	s.min_value = minv
	s.max_value = maxv
	s.step = step
	s.value = value
	s.suffix = suffix
	s.add_theme_font_size_override("font_size", 12)
	s.value_changed.connect(func(v: float) -> void:
		if _syncing:
			return
		on_change.call(v))
	_row(label, s)
	return s


## 一行三个数字框（Vector3）。改动后把整行的当前值打成一个 Vector3 回调出去。
func _vec3_row(label: String, value: Vector3, on_change: Callable, suffix: String) -> void:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 4)
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(88, 0)
	l.add_theme_font_size_override("font_size", 12)
	line.add_child(l)
	var spins := []
	for i in 3:
		var s := SpinBox.new()
		s.min_value = -1000.0
		s.max_value = 1000.0
		s.step = 0.05
		s.value = [value.x, value.y, value.z][i]
		s.suffix = suffix
		s.custom_minimum_size = Vector2(60, 0)
		s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		s.add_theme_font_size_override("font_size", 12)
		spins.append(s)
		line.add_child(s)
	var read := func() -> Vector3:
		return Vector3((spins[0] as SpinBox).value, (spins[1] as SpinBox).value,
				(spins[2] as SpinBox).value)
	for s in spins:
		(s as SpinBox).value_changed.connect(func(_v: float) -> void:
			if _syncing:
				return
			on_change.call(read.call()))
	_box.add_child(line)


func _color_row(label: String, hex: String, on_change: Callable) -> void:
	var pick := ColorPickerButton.new()
	pick.color = Scene3D.color_of(hex, Color.WHITE)
	pick.custom_minimum_size = Vector2(0, 24)
	pick.color_changed.connect(func(c: Color) -> void:
		if _syncing:
			return
		on_change.call("#" + c.to_html(c.a < 0.999)))
	_row(label, pick)


func _check_row(label: String, value: bool, on_change: Callable) -> void:
	var c := CheckBox.new()
	c.button_pressed = value
	c.add_theme_font_size_override("font_size", 12)
	c.toggled.connect(func(on: bool) -> void:
		if _syncing:
			return
		on_change.call(on))
	_row(label, c)


func _section(title: String) -> void:
	var l := Label.new()
	l.text = title
	l.add_theme_font_size_override("font_size", 13)
	l.modulate = Color(0.8, 0.86, 1.0)
	_box.add_child(l)


func _note(text: String) -> void:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(0, 0)
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	l.modulate = Color(1, 1, 1, 0.55)
	l.add_theme_font_size_override("font_size", 12)
	_box.add_child(l)
