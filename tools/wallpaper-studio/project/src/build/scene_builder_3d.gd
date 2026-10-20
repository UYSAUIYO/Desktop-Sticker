extends RefCounted
## 3D 文档 -> Node3D 场景树。**预览和导出共用这一条路径**（与 2D 的 SceneBuilder 同一不变式）。
##
## 动画同样只有**一个求值器**：相机路径与物体的自转 / 浮动 / 环绕都在这里按帧率
## 采样成 AnimationPlayer 的 position / rotation 轨道（和 2D 把 ease 烘焙成线性关键帧
## 是同一个思路）—— 拖播放头看到什么，桌面上就是什么，导出包里没有任何运行时脚本。
##
## 物体旋转为什么不用四元数轨道：3D 编辑器里用户按"度"填欧拉角，文档里存的也是欧拉角；
## 逐帧采样后轨道值就是每帧的欧拉角，插值在相邻两帧（1/60s）之间做，误差不可见。

const Scene3D = preload("res://src/doc/scene3d.gd")

const ROOT_NAME := "WallpaperRoot3D"
const NODE_PREFIX := "O_"          # 物体节点名前缀 + 物体 id
const ANIM_NAME := "Anim"
const PLAYER_NAME := "Player"

## 采样帧率与总帧数上限。600s 的循环按 60Hz 采会爆内存，超出就降帧。
const BAKE_HZ := 60.0
const BAKE_MAX_FRAMES := 1800


## project_dir：用户工程目录（预览）或构建目录的 assets 目录（导出）。
## autoplay=false 时停在 0 帧，由预览自己拖播放头。
static func build(doc, project_dir: String, autoplay: bool = true) -> Node3D:
	var root := Node3D.new()
	root.name = ROOT_NAME
	root.set_process(false)
	root.set_physics_process(false)

	root.add_child(_build_environment(doc.env3d))
	for o in doc.objects3d:
		if not (o is Dictionary) or not bool(o.get("visible", true)):
			continue
		var node := build_object(o, project_dir)
		if node != null:
			node.name = NODE_PREFIX + String(o.get("id", ""))
			root.add_child(node)

	var cam := Camera3D.new()
	cam.name = "Camera"
	cam.current = true
	cam.fov = float(doc.camera3d.get("fov", 55.0))
	var st = Scene3D.camera_at(doc.camera3d, 0.0, doc.loop_seconds)
	cam.position = st["pos"]
	root.add_child(cam)
	_aim(cam, st["target"])

	_compile_animation(doc, root, cam, autoplay)
	_assign_owners(root)
	return root


# ---------------------------------------------------------------- 环境

static func _build_environment(env: Dictionary) -> WorldEnvironment:
	var we := WorldEnvironment.new()
	we.name = "Env"
	var e := Environment.new()
	var sky := Sky.new()
	var mat := ProceduralSkyMaterial.new()
	mat.sky_top_color = Scene3D.color_of(String(env.get("sky_top", "#0a1428")),
			Color(0.04, 0.08, 0.16))
	mat.sky_horizon_color = Scene3D.color_of(String(env.get("sky_bottom", "#1c2c44")),
			Color(0.11, 0.17, 0.27))
	mat.ground_bottom_color = mat.sky_horizon_color
	mat.ground_horizon_color = mat.sky_horizon_color
	mat.energy_multiplier = float(env.get("sky_energy", 1.0))
	sky.sky_material = mat
	e.background_mode = Environment.BG_SKY
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_energy = float(env.get("ambient", 0.35))
	if bool(env.get("fog", false)):
		e.fog_enabled = true
		e.fog_density = float(env.get("fog_density", 0.02))
	if bool(env.get("glow", true)):
		e.glow_enabled = true
		e.glow_intensity = float(env.get("glow_intensity", 0.5))
	we.environment = e
	return we


# ---------------------------------------------------------------- 物体

## 单个物体 -> 节点。transform（不含动画）在这里落；返回 null 表示跳过。
static func build_object(o: Dictionary, project_dir: String) -> Node3D:
	var type := String(o.get("type", "box"))
	var node: Node3D
	var mesh_node: MeshInstance3D = null
	match type:
		"box":
			node = _mesh(BoxMesh.new(), o)
		"sphere":
			var sm := SphereMesh.new()
			sm.radius = maxf(0.01, _p(o, "radius", 0.6))
			sm.height = sm.radius * 2.0
			sm.radial_segments = int(_p(o, "segments", 48))
			sm.rings = maxi(8, int(_p(o, "segments", 48)) / 2)
			node = _mesh(sm, o)
		"cylinder":
			var cm := CylinderMesh.new()
			cm.top_radius = maxf(0.01, _p(o, "radius", 0.5))
			cm.bottom_radius = cm.top_radius
			cm.height = maxf(0.01, _p(o, "height", 1.2))
			cm.radial_segments = int(_p(o, "segments", 48))
			node = _mesh(cm, o)
		"torus":
			var tm := TorusMesh.new()
			tm.inner_radius = maxf(0.01, _p(o, "inner", 0.35))
			tm.outer_radius = maxf(0.02, _p(o, "outer", 0.8))
			tm.rings = int(_p(o, "segments", 64))
			node = _mesh(tm, o)
		"plane":
			var pm := PlaneMesh.new()
			var sz: Array = _vec3(o.get("params", {}).get("size", null), [12.0, 12.0, 0.0])
			pm.size = Vector2(maxf(0.01, sz[0]), maxf(0.01, sz[1]))
			node = _mesh(pm, o)
		"text":
			node = _label(o)
		"light_sun":
			node = _light_sun(o)
		"light_omni":
			node = _light_omni(o)
		"model":
			node = _model(o, project_dir)
		_:
			return null
	node.position = Scene3D.vec3(o.get("pos", [0.0, 0.0, 0.0]))
	var rot: Array = o.get("rot", [0.0, 0.0, 0.0])
	node.rotation_degrees = Vector3(float(rot[0]), float(rot[1]), float(rot[2]))
	node.scale = Scene3D.vec3(o.get("scale", [1.0, 1.0, 1.0]))
	return node


static func _mesh(mesh: Mesh, o: Dictionary) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = _material(o.get("material", {}))
	return mi


static func _material(mat: Dictionary) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Scene3D.color_of(String(mat.get("albedo", "#c8ccd4")), Color(0.78, 0.8, 0.83))
	m.metallic = clampf(float(mat.get("metallic", 0.0)), 0.0, 1.0)
	m.roughness = clampf(float(mat.get("roughness", 0.55)), 0.0, 1.0)
	var em := Scene3D.color_of(String(mat.get("emission", "#000000")), Color.BLACK)
	if em.get_luminance() > 0.001:
		m.emission_enabled = true
		m.emission = em
		m.emission_energy_multiplier = maxf(0.0, float(mat.get("emission_energy", 1.0)))
	var alpha := clampf(float(mat.get("alpha", 1.0)), 0.0, 1.0)
	if alpha < 0.999:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.albedo_color.a = alpha
	return m


static func _label(o: Dictionary) -> Node3D:
	var p: Dictionary = o.get("params", {})
	var l := Label3D.new()
	l.text = String(p.get("text", "文本"))
	l.font_size = int(p.get("font_size", 96))
	l.modulate = Scene3D.color_of(String(p.get("color", "#ffffff")), Color.WHITE)
	l.outline_size = int(p.get("outline", 0))
	l.billboard = BaseMaterial3D.BILLBOARD_ENABLED if bool(p.get("billboard", true)) \
			else BaseMaterial3D.BILLBOARD_DISABLED
	l.pixel_size = 0.005
	# 文字没有 PBR 材质，material 字段忽略。
	return l


static func _light_sun(o: Dictionary) -> Node3D:
	var l := DirectionalLight3D.new()
	var p: Dictionary = o.get("params", {})
	l.light_energy = maxf(0.0, float(p.get("energy", 1.2)))
	l.light_color = Scene3D.color_of(String(p.get("color", "#ffffff")), Color.WHITE)
	l.light_angle_max = deg_to_rad(clampf(float(p.get("angle", 30.0)), 0.0, 89.0))
	# rot 已经在 build_object 里落（DirectionalLight3D -Z 是照射方向）。
	return l


static func _light_omni(o: Dictionary) -> Node3D:
	var l := OmniLight3D.new()
	var p: Dictionary = o.get("params", {})
	l.light_energy = maxf(0.0, float(p.get("energy", 2.0)))
	l.light_color = Scene3D.color_of(String(p.get("color", "#ffd9a0")), Color(1.0, 0.85, 0.63))
	l.omni_range = maxf(0.1, float(p.get("range", 8.0)))
	return l


## .glb / .gltf / .obj：template_release 里没有编辑器导入管线，只能运行时解析。
## GLTFDocument 在模板运行库里可用；.obj 用 ObjFileParser 兜不住，直接报 null。
static func _model(o: Dictionary, project_dir: String) -> Node3D:
	var p: Dictionary = o.get("params", {})
	var rel := String(p.get("src", ""))
	var holder := Node3D.new()
	if rel.is_empty():
		return holder
	var abs_path := rel
	if not abs_path.is_absolute_path():
		abs_path = project_dir.path_join(rel)
	abs_path = ProjectSettings.globalize_path(abs_path)
	var parsed = _load_model(abs_path)
	if parsed != null:
		holder.add_child(parsed)
	var normalize := maxf(0.0, float(p.get("normalize", 1.0)))
	if normalize > 0.0 and parsed != null:
		# 模型尺寸没有标准：统一缩到 normalize 米高（0 = 不动）。
		var aabb: AABB = parsed.get_aabb()
		var h := maxf(0.0001, aabb.size.y)
		var k := normalize / h
		parsed.scale = Vector3(k, k, k)
		parsed.position = -aabb.get_center() * k
	return holder


## 返回一个包着模型的 Node3D（带 AABB 信息）；失败返回 null。
static func _load_model(abs_path: String):
	var lower := abs_path.to_lower()
	if lower.ends_with(".glb") or lower.ends_with(".gltf"):
		var doc := GLTFDocument.new()
		var state := GLTFState.new()
		if doc.append_from_file(abs_path, state) != OK:
			return null
		var roots := doc.generate_scene(state)
		if roots == null:
			return null
		var holder := Node3D.new()
		holder.add_child(roots)
		return holder
	if lower.ends_with(".obj"):
		var obj := load(abs_path)
		if obj is Mesh:
			var mi := MeshInstance3D.new()
			mi.mesh = obj
			var holder2 := Node3D.new()
			holder2.add_child(mi)
			return holder2
	return null


static func _p(o: Dictionary, key: String, fallback: float) -> float:
	return float((o.get("params", {}) as Dictionary).get(key, fallback))


static func _vec3(raw, fallback: Array) -> Array:
	if raw is Array and (raw as Array).size() >= 3:
		return [float(raw[0]), float(raw[1]), float(raw[2])]
	return fallback


# ---------------------------------------------------------------- 动画

## 把相机路径与物体的参数化动画采样成轨道。时长 = doc.loop_seconds（循环回绕）。
static func _compile_animation(doc, root: Node3D, cam: Camera3D, autoplay: bool) -> void:
	var total := maxf(0.25, doc.loop_seconds)
	var frames := clampi(int(ceil(total * BAKE_HZ)), 2, BAKE_MAX_FRAMES)
	var hz := float(frames) / total
	var anim := Animation.new()
	anim.length = total

	var cam_path := root.get_path_to(cam)
	var pos_track := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(pos_track, "%s:position" % cam_path)
	var rot_track := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(rot_track, "%s:rotation" % cam_path)
	var fov_track := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(fov_track, "%s:fov" % cam_path)
	anim.track_set_interpolation_type(pos_track, Animation.INTERPOLATION_LINEAR)
	anim.track_set_interpolation_type(rot_track, Animation.INTERPOLATION_LINEAR)
	var probe := Camera3D.new()
	for i in frames + 1:
		var t := minf(total, float(i) / hz)
		var st = Scene3D.camera_at(doc.camera3d, t, total)
		anim.track_insert_key(pos_track, t, st["pos"])
		probe.position = st["pos"]
		_aim(probe, st["target"])
		anim.track_insert_key(rot_track, t, probe.rotation)
		anim.track_insert_key(fov_track, t, float(st["fov"]))
	probe.free()

	for o in doc.objects3d:
		if not (o is Dictionary) or not bool(o.get("visible", true)):
			continue
		var node := root.get_node_or_null(NodePath(NODE_PREFIX + String(o.get("id", ""))))
		if node == null:
			continue
		_bake_object(anim, doc, root, node, o, total, frames, hz)

	var lib := AnimationLibrary.new()
	lib.add_animation("wallpaper", anim)
	var player := AnimationPlayer.new()
	player.name = PLAYER_NAME
	player.add_animation_library("", lib)
	root.add_child(player)
	player.playback_default_blend_time = 0.0
	if autoplay:
		# 导出后由引擎自己开播（autoplay 属性会随 PackedScene 一起打包）；
		# 预览传 false，由预览自己拖播放头（set_player_time）。
		player.autoplay = "wallpaper"


## 一个物体的自转 / 浮动 / 环绕（全部可选，全 0 就是静止）。
static func _bake_object(anim: Animation, doc, root: Node3D, node: Node3D,
		o: Dictionary, total: float, frames: int, hz: float) -> void:
	var a: Dictionary = o.get("anim", {})
	if a.is_empty():
		return
	var spin: Array = a.get("spin", [0.0, 0.0, 0.0])
	var bob := float(a.get("bob", 0.0))
	var bob_speed := float(a.get("bob_speed", 1.0))
	var orbit: Dictionary = a.get("orbit", {})
	var radius := float(orbit.get("radius", 0.0))
	var ospeed := float(orbit.get("speed", 0.5))
	var oheight := float(orbit.get("height", 0.0))
	if Vector3(float(spin[0]), float(spin[1]), float(spin[2])) == Vector3.ZERO \
			and bob <= 0.0 and radius <= 0.0:
		return
	# 环绕的圆心 = 物体自己的初始位置。
	var center := Scene3D.vec3(o.get("pos", [0.0, 0.0, 0.0]))
	var base_rot: Array = o.get("rot", [0.0, 0.0, 0.0])
	var path := root.get_path_to(node)
	var pt := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(pt, "%s:position" % path)
	var rt := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(rt, "%s:rotation" % path)
	anim.track_set_interpolation_type(pt, Animation.INTERPOLATION_LINEAR)
	anim.track_set_interpolation_type(rt, Animation.INTERPOLATION_LINEAR)
	# 自转按文档里写的角速度匀速累积，循环时长结束时正好转了 spin * total 度 ——
	# 循环接缝是连续的（数值是 360 的整数倍时视觉上完全无缝）。
	# 轨道里存的是属性值：Godot 的 rotation 属性是**弧度**，文档里存的是度。
	var rot0 := Vector3(float(base_rot[0]), float(base_rot[1]), float(base_rot[2]))
	for i in frames + 1:
		var t := minf(total, float(i) / hz)
		var pos := center
		if radius > 0.0:
			var ang := t * ospeed
			pos += Vector3(cos(ang) * radius, oheight * sin(t * ospeed * 0.5),
					sin(ang) * radius)
		if bob > 0.0:
			pos.y += sin(t * bob_speed * TAU / maxf(0.01, total)) * bob
		var rot_deg := rot0 + Vector3(float(spin[0]), float(spin[1]), float(spin[2])) * t
		anim.track_insert_key(pt, t, pos)
		anim.track_insert_key(rt, t, rot_deg * PI / 180.0)


## 把播放头放到 t（没有播放器返回 false）。与 2D 的 AnimationBuilder.set_player_time
## 同一套：先把动画"装"上再暂停，之后重复调用只做 seek —— 拖动时不会每帧重新起播。
static func set_player_time(player: AnimationPlayer, t: float) -> bool:
	if player == null:
		return false
	if player.current_animation != "wallpaper":
		player.play("wallpaper")
		player.pause()
	player.seek(t, true)
	return true


## 让 node 朝向 target（保持 up = +Y）。相机 / 灯都看 -Z。
## 用 look_at_from_position 而不是 look_at：场景树在构建时还没进 SceneTree，
## look_at 会报 "Node not inside tree"（导出时刷屏）。
static func _aim(node: Node3D, target: Vector3) -> void:
	var dir := target - node.position
	if dir.length() < 0.0001:
		return
	if absf(dir.normalized().dot(Vector3.UP)) > 0.999:
		# 正对上/下时 look_at 不稳定：稍微偏一点。
		dir += Vector3(0.001, 0.0, 0.001)
	node.look_at_from_position(node.position, target, Vector3.UP)


## PackedScene.pack 前调用：把整棵树的 owner 指到根（不在树里时得手动设）。
static func _assign_owners(root: Node) -> void:
	for child in root.get_children():
		child.owner = root
		_assign_owners(child)
