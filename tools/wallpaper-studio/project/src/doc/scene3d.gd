extends RefCounted
## 3D 场景（mode = "3d"）的文档模型。
##
## 2D 文档的图层树在 3D 里没有对应物：3D 壁纸是"一堆物体 + 一个相机 + 一个环境"，
## 没有容器嵌套（父级关系用不了几次，却把选择/命中测试/坐标换算全部复杂化）。
## 所以 3D 模式有自己的三个顶层结构，与 `layers` 并列、互不干扰：
##
##   objects3d : [{id, name, type, visible, pos, rot, scale, material, params, anim}]
##   camera3d  : {pos, target, fov, keys: [{t, pos, target, fov, ease}], play}
##   env3d     : {sky_top, sky_bottom, ambient, light…, fog…, glow…, ground…}
##
## 约定与 2D 保持一致的地方：
##   · JSON 是唯一真源，所有字段缺失都取默认值（normalize 永远兜底）；
##   · 单位：位置用米，旋转用度，fov 用度；
##   · 动画只有"编译期确定"的一种：相机路径按关键帧插值，物体只有自转/浮动/环绕
##     三种参数化动画（在运行时脚本里算，不占关键帧通道）—— 壁纸要的是循环的氛围，
##     不是逐帧动画编辑器。
##
## 本文件是纯函数集合，无头可测（不碰任何节点）。

const OBJ_TYPES := ["box", "sphere", "cylinder", "torus", "plane", "text",
		"light_sun", "light_omni", "model"]
## 有自己几何、参与"场景里有没有东西"判断的类型（光源不算）。
const CONTENT_TYPES := ["box", "sphere", "cylinder", "torus", "plane", "text", "model"]
const LIGHT_TYPES := ["light_sun", "light_omni"]

const PLAY_MODES := ["loop", "mirror", "once"]
const EASES := ["linear", "in", "out", "inout"]

const MAX_OBJECTS := 128
const MAX_KEYS := 64


# ---------------------------------------------------------------- 基础

static func _base(type: String, obj_name: String) -> Dictionary:
	return {
		"id": "o_" + _new_id(),
		"type": type,
		"name": obj_name,
		"visible": true,
		"pos": [0.0, 0.0, 0.0],
		"rot": [0.0, 0.0, 0.0],
		"scale": [1.0, 1.0, 1.0],
		"material": {
			"albedo": "#c8ccd4",
			"metallic": 0.0,
			"roughness": 0.55,
			"emission": "#000000",
			"emission_energy": 1.0,
			"alpha": 1.0,
		},
		"params": {},
		"anim": {"spin": [0.0, 0.0, 0.0], "bob": 0.0, "bob_speed": 1.0,
				"orbit": {"radius": 0.0, "speed": 0.5, "height": 0.0}},
	}


static func _new_id() -> String:
	return "%08x" % (randi() & 0xFFFFFFFF)


static func is_content(obj: Dictionary) -> bool:
	return String(obj.get("type", "")) in CONTENT_TYPES


static func is_light(obj: Dictionary) -> bool:
	return String(obj.get("type", "")) in LIGHT_TYPES


# ---------------------------------------------------------------- 构造

static func make_box(obj_name: String) -> Dictionary:
	var o := _base("box", obj_name)
	o["params"] = {"size": [1.0, 1.0, 1.0]}
	return o


static func make_sphere(obj_name: String) -> Dictionary:
	var o := _base("sphere", obj_name)
	o["params"] = {"radius": 0.6, "segments": 48}
	return o


static func make_cylinder(obj_name: String) -> Dictionary:
	var o := _base("cylinder", obj_name)
	o["params"] = {"radius": 0.5, "height": 1.2, "segments": 48}
	return o


static func make_torus(obj_name: String) -> Dictionary:
	var o := _base("torus", obj_name)
	o["params"] = {"inner": 0.35, "outer": 0.8, "segments": 64}
	return o


static func make_plane(obj_name: String) -> Dictionary:
	var o := _base("plane", obj_name)
	o["params"] = {"size": [12.0, 12.0]}
	o["material"]["roughness"] = 0.9
	return o


static func make_text(obj_name: String) -> Dictionary:
	var o := _base("text", obj_name)
	o["params"] = {"text": obj_name, "font_size": 96, "color": "#ffffff",
			"outline": 0, "billboard": true}
	o["material"] = {}
	return o


static func make_light_sun(obj_name: String) -> Dictionary:
	var o := _base("light_sun", obj_name)
	o["params"] = {"energy": 1.2, "color": "#ffffff", "angle": 30.0}
	o["material"] = {}
	o["anim"] = {}
	o["rot"] = [-40.0, 30.0, 0.0]
	return o


static func make_light_omni(obj_name: String) -> Dictionary:
	var o := _base("light_omni", obj_name)
	o["params"] = {"energy": 2.0, "color": "#ffd9a0", "range": 8.0}
	o["material"] = {}
	o["anim"] = {}
	return o


## 导入一个模型文件（.glb / .gltf / .obj）。src 是**相对工程目录**的路径。
static func make_model(obj_name: String, src: String) -> Dictionary:
	var o := _base("model", obj_name)
	o["params"] = {"src": src, "normalize": 1.0}
	o["material"] = {}
	return o


## 新场景的初始内容：地面 + 中心球 + 一盏平行光 + 一个点光，相机对着中心。
static func starter_scene() -> Array:
	var ground := make_plane("地面")
	ground["pos"] = [0.0, -0.75, 0.0]
	var core := make_sphere("主体")
	core["material"]["albedo"] = "#3f7fd4"
	core["material"]["metallic"] = 0.35
	core["material"]["roughness"] = 0.3
	var ring := make_torus("环")
	ring["pos"] = [0.0, 0.0, 0.0]
	ring["rot"] = [72.0, 0.0, 0.0]
	ring["scale"] = [1.6, 1.6, 1.6]
	ring["material"]["albedo"] = "#e0a13f"
	ring["material"]["metallic"] = 0.8
	ring["material"]["roughness"] = 0.25
	ring["anim"]["spin"] = [0.0, 40.0, 0.0]
	var sun := make_light_sun("主光")
	return [ground, core, ring, sun]


# ---------------------------------------------------------------- 规范化

static func normalize_object(raw: Dictionary) -> Dictionary:
	var type := String(raw.get("type", "box"))
	if not (type in OBJ_TYPES):
		type = "box"
	var out := _base(type, String(raw.get("name", "物体")))
	out["id"] = String(raw.get("id", out["id"]))
	if out["id"].is_empty():
		out["id"] = "o_" + _new_id()
	out["visible"] = bool(raw.get("visible", true))
	out["pos"] = _vec3_of(raw.get("pos"), out["pos"])
	out["rot"] = _vec3_of(raw.get("rot"), out["rot"])
	var s := _vec3_of(raw.get("scale"), out["scale"])
	out["scale"] = [maxf(0.001, s[0]), maxf(0.001, s[1]), maxf(0.001, s[2])]
	var mat := raw.get("material")
	if mat is Dictionary:
		var m: Dictionary = mat
		var base: Dictionary = out["material"]
		for k in base.keys():
			if base[k] is String:
				base[k] = _color_of(m.get(k), base[k])
			elif base[k] is float or base[k] is int:
				base[k] = float(m.get(k, base[k]))
		out["material"] = base
	if raw.get("params") is Dictionary:
		var p: Dictionary = raw["params"]
		for k in p.keys():
			out["params"][k] = p[k]
	if raw.get("anim") is Dictionary:
		var a: Dictionary = raw["anim"]
		var an: Dictionary = out["anim"]
		an["spin"] = _vec3_of(a.get("spin"), an["spin"])
		an["bob"] = maxf(0.0, float(a.get("bob", an["bob"])))
		an["bob_speed"] = maxf(0.0, float(a.get("bob_speed", an["bob_speed"])))
		if a.get("orbit") is Dictionary:
			var orb: Dictionary = a["orbit"]
			(an["orbit"] as Dictionary)["radius"] = maxf(0.0, float(orb.get("radius", 0.0)))
			(an["orbit"] as Dictionary)["speed"] = float(orb.get("speed", 0.5))
			(an["orbit"] as Dictionary)["height"] = float(orb.get("height", 0.0))
	return out


static func normalize_objects(raw) -> Array:
	var out := []
	if not (raw is Array):
		return out
	for item in raw:
		if item is Dictionary:
			out.append(normalize_object(item))
	return out


## 相机。keys 非空时路径生效（否则就是固定机位）。
static func normalize_camera(raw) -> Dictionary:
	var out := {
		"pos": [0.0, 1.6, 5.0],
		"target": [0.0, 0.2, 0.0],
		"fov": 55.0,
		"keys": [],
		"play": "loop",
	}
	if not (raw is Dictionary):
		return out
	out["pos"] = _vec3_of(raw.get("pos"), out["pos"])
	out["target"] = _vec3_of(raw.get("target"), out["target"])
	out["fov"] = clampf(float(raw.get("fov", out["fov"])), 10.0, 150.0)
	var play := String(raw.get("play", "loop"))
	out["play"] = play if play in PLAY_MODES else "loop"
	if raw.get("keys") is Array:
		var keys := []
		for k in raw["keys"]:
			if not (k is Dictionary):
				continue
			keys.append({
				"t": maxf(0.0, float(k.get("t", 0.0))),
				"pos": _vec3_of(k.get("pos"), out["pos"]),
				"target": _vec3_of(k.get("target"), out["target"]),
				"fov": clampf(float(k.get("fov", out["fov"])), 10.0, 150.0),
				"ease": _ease_of(String(k.get("ease", "linear"))),
			})
		keys.sort_custom(func(a, b) -> bool: return float(a["t"]) < float(b["t"]))
		out["keys"] = keys.slice(0, MAX_KEYS)
	return out


static func _ease_of(s: String) -> String:
	return s if s in EASES else "linear"


static func normalize_env(raw) -> Dictionary:
	var out := {
		"sky_top": "#0a1428",
		"sky_bottom": "#1c2c44",
		"sky_energy": 1.0,
		"ambient": 0.35,
		"fog": false,
		"fog_density": 0.02,
		"glow": true,
		"glow_intensity": 0.5,
		"ground_grid": false,
	}
	if not (raw is Dictionary):
		return out
	out["sky_top"] = _color_of(raw.get("sky_top"), out["sky_top"])
	out["sky_bottom"] = _color_of(raw.get("sky_bottom"), out["sky_bottom"])
	out["sky_energy"] = clampf(float(raw.get("sky_energy", 1.0)), 0.0, 8.0)
	out["ambient"] = clampf(float(raw.get("ambient", 0.35)), 0.0, 4.0)
	out["fog"] = bool(raw.get("fog", false))
	out["fog_density"] = clampf(float(raw.get("fog_density", 0.02)), 0.0, 1.0)
	out["glow"] = bool(raw.get("glow", true))
	out["glow_intensity"] = clampf(float(raw.get("glow_intensity", 0.5)), 0.0, 4.0)
	out["ground_grid"] = bool(raw.get("ground_grid", false))
	return out


# ---------------------------------------------------------------- 取值 / 类型转换

static func _vec3_of(raw, fallback: Array) -> Array:
	if raw is Array and (raw as Array).size() >= 3:
		return [float(raw[0]), float(raw[1]), float(raw[2])]
	return [float(fallback[0]), float(fallback[1]), float(fallback[2])]


static func _color_of(raw, fallback: String) -> String:
	if raw is String:
		var s: String = raw.strip_edges()
		if s.begins_with("#") and s.length() >= 7:
			return s.substr(0, 9)
	if raw is Color:
		return "#" + (raw as Color).to_html((raw as Color).a < 0.999)
	return fallback


static func vec3(arr: Array) -> Vector3:
	return Vector3(float(arr[0]), float(arr[1]), float(arr[2]))


static func color_of(hex: String, fallback: Color) -> Color:
	var s := hex.strip_edges().trim_prefix("#")
	if (s.length() == 6 or s.length() == 8) and s.is_valid_hex_number(false):
		return Color.html(s)
	return fallback


# ---------------------------------------------------------------- 相机路径

## t 时刻（**秒**）的相机状态 {pos, target, fov}。关键帧的 t 也是秒，
## 和 x/y 关键帧、循环时长同一个时间轴（超出 loop 的帧不会播，validate 会拦）。
## 没关键帧时返回静态机位。
static func camera_at(cam: Dictionary, t: float, loop_seconds: float) -> Dictionary:
	var keys: Array = cam.get("keys", [])
	if keys.is_empty():
		return {"pos": vec3(cam["pos"]), "target": vec3(cam["target"]),
				"fov": float(cam["fov"])}
	var total := maxf(0.25, loop_seconds)
	var play := String(cam.get("play", "loop"))
	# once：播完停在末尾；loop：回绕；mirror：往返（一个来回 = 2 × 总长）。
	var tt := clampf(t, 0.0, total)
	if play == "loop":
		tt = fposmod(t, total)
	elif play == "mirror":
		var m := fposmod(t, total * 2.0)
		tt = m if m <= total else total * 2.0 - m
	var a: Dictionary = keys[0]
	var b: Dictionary = keys[keys.size() - 1]
	if tt <= float(a["t"]):
		return _cam_state(a)
	if tt >= float(b["t"]):
		return _cam_state(b)
	for i in range(keys.size() - 1):
		var k0: Dictionary = keys[i]
		var k1: Dictionary = keys[i + 1]
		if tt >= float(k0["t"]) and tt <= float(k1["t"]):
			var span := maxf(0.0001, float(k1["t"]) - float(k0["t"]))
			var f := (tt - float(k0["t"])) / span
			f = _ease_apply(String(k1.get("ease", "linear")), f)
			return {
				"pos": _lerp3(vec3(k0["pos"]), vec3(k1["pos"]), f),
				"target": _lerp3(vec3(k0["target"]), vec3(k1["target"]), f),
				"fov": lerpf(float(k0["fov"]), float(k1["fov"]), f),
			}
	return _cam_state(b)


static func _cam_state(k: Dictionary) -> Dictionary:
	return {"pos": vec3(k["pos"]), "target": vec3(k["target"]), "fov": float(k["fov"])}


static func _lerp3(a: Vector3, b: Vector3, f: float) -> Vector3:
	return a + (b - a) * f


static func _ease_apply(ease: String, f: float) -> float:
	match ease:
		"in":
			return f * f * f
		"out":
			return 1.0 - pow(1.0 - f, 3.0)
		"inout":
			return f * f * (3.0 - 2.0 * f)
		_:
			return f


## 相机路径关键帧的结束时刻（超出循环时长的帧不会播，validate 用）。
static func camera_key_end(cam: Dictionary) -> float:
	var keys: Array = cam.get("keys", [])
	return 0.0 if keys.is_empty() else float(keys[keys.size() - 1]["t"])


## 在 t 处写一个相机关键帧（覆盖同刻的旧帧）。cam 是文档里的 camera3d 字典。
static func set_camera_key(cam: Dictionary, t: float) -> void:
	var keys: Array = cam.get("keys", [])
	for k in keys:
		if absf(float(k["t"]) - t) < 0.001:
			k["pos"] = [cam["pos"][0], cam["pos"][1], cam["pos"][2]]
			k["target"] = [cam["target"][0], cam["target"][1], cam["target"][2]]
			k["fov"] = float(cam["fov"])
			return
	keys.append({"t": t, "pos": [cam["pos"][0], cam["pos"][1], cam["pos"][2]],
			"target": [cam["target"][0], cam["target"][1], cam["target"][2]],
			"fov": float(cam["fov"]), "ease": "linear"})
	keys.sort_custom(func(a, b) -> bool: return float(a["t"]) < float(b["t"]))
	cam["keys"] = keys


static func remove_camera_key(cam: Dictionary, t: float) -> void:
	var keys: Array = cam.get("keys", [])
	for i in keys.size():
		if absf(float(keys[i]["t"]) - t) < 0.001:
			keys.remove_at(i)
			cam["keys"] = keys
			return


# ---------------------------------------------------------------- 序列化

## 写盘前把 Color 兜底转掉（3D 模型的参数里没有 Color 对象，但 params 是自由字典，
## 用户数据什么都有可能进来 —— 和 Layer.to_wire 同一个理由）。
static func to_wire(v):
	if v is Color:
		var c: Color = v
		return "#" + c.to_html(c.a < 0.999)
	if v is Vector3:
		var w: Vector3 = v
		return [w.x, w.y, w.z]
	if v is Dictionary:
		var out := {}
		for k in v:
			out[k] = to_wire(v[k])
		return out
	if v is Array:
		var out2 := []
		for item in v:
			out2.append(to_wire(item))
		return out2
	return v
