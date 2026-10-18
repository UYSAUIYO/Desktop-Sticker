extends RefCounted
## 单个图层。用 Dictionary 承载（不是 class），因为 JSON 是文档的真源，
## 保持"文档 <-> 字典"零阻抗比类型安全更重要。

const StudioPaths = preload("res://src/doc/paths.gd")

const TYPES := ["color", "image", "text", "particles"]
const BLENDS := ["mix", "add", "sub", "mul", "screen", "overlay"]
const CHANNELS := ["x", "y", "scale", "rotation", "opacity"]

static func _base(type: String, layer_name: String) -> Dictionary:
	return {
		"id": "l_" + StudioPaths.new_pack_id().substr(0, 8),
		"type": type,
		"name": layer_name,
		"visible": true,
		# w / h = 0 表示"还没定过尺寸"，语义是铺满画布（见 doc/frame.gd）。
		"transform": {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0,
				"scale": 1.0, "rotation": 0.0, "opacity": 1.0},
		"blend": "mix",
		"tracks": [],
		"effects": [],
		"script_path": "",
		"shader_path": "",
	}

static func make_color(layer_name: String, color: Color) -> Dictionary:
	var l := _base("color", layer_name)
	l["color"] = color
	return l

static func make_image(layer_name: String, src: String) -> Dictionary:
	var l := _base("image", layer_name)
	l["src"] = src
	l["fit"] = "contain"
	l["speed"] = 1.0
	return l

static func make_text(layer_name: String, text: String) -> Dictionary:
	var l := _base("text", layer_name)
	l["text"] = text
	l["font_size"] = 64
	l["color"] = Color(0.9, 0.95, 1.0)
	l["align"] = "center"
	l["dynamic"] = "none"
	l["date_format"] = "%04d-%02d-%02d"
	return l

static func make_particles(layer_name: String, texture: String) -> Dictionary:
	var l := _base("particles", layer_name)
	l["texture"] = texture
	l["amount"] = 120
	l["lifetime"] = 6.0
	l["direction"] = {"x": 0.0, "y": -1.0}
	l["spread"] = 45.0
	l["velocity"] = 40.0
	l["gravity"] = {"x": 0.0, "y": 20.0}
	l["scale_min"] = 0.4
	l["scale_max"] = 1.0
	return l

## 把字典里缺失/类型不对的字段补成默认值。纯函数，返回新字典。
static func normalize(raw: Dictionary) -> Dictionary:
	var type := String(raw.get("type", "color"))
	if not (type in TYPES):
		type = "color"
	var out := _base(type, String(raw.get("name", "图层")))
	out["id"] = String(raw.get("id", out["id"]))
	if out["id"].is_empty():
		out["id"] = "l_" + StudioPaths.new_pack_id().substr(0, 8)
	out["visible"] = bool(raw.get("visible", true))
	var blend := String(raw.get("blend", "mix"))
	out["blend"] = blend if blend in BLENDS else "mix"
	out["script_path"] = String(raw.get("script_path", ""))
	out["shader_path"] = String(raw.get("shader_path", ""))
	if raw.get("transform") is Dictionary:
		var t: Dictionary = raw["transform"]
		for k in ["x", "y", "w", "h", "rotation", "scale", "opacity"]:
			out["transform"][k] = float(t.get(k, out["transform"][k]))
	if raw.get("tracks") is Array:
		out["tracks"] = raw["tracks"]
	if raw.get("effects") is Array:
		out["effects"] = raw["effects"]
	match type:
		"color":
			out["color"] = to_color(raw.get("color"), Color(0.02, 0.07, 0.12))
		"image":
			out["src"] = String(raw.get("src", ""))
			var fit := String(raw.get("fit", "contain"))
			out["fit"] = fit if fit in ["fill", "contain", "cover", "tile"] else "contain"
			out["speed"] = float(raw.get("speed", 1.0))
		"text":
			out["text"] = String(raw.get("text", "文本"))
			out["font_size"] = int(raw.get("font_size", 64))
			out["color"] = to_color(raw.get("color"), Color(0.9, 0.95, 1.0))
			out["align"] = String(raw.get("align", "center"))
			out["dynamic"] = String(raw.get("dynamic", "none"))
			out["date_format"] = String(raw.get("date_format", "%04d-%02d-%02d"))
		"particles":
			out["texture"] = String(raw.get("texture", ""))
			out["amount"] = int(raw.get("amount", 120))
			out["lifetime"] = float(raw.get("lifetime", 6.0))
			out["direction"] = to_vec2(raw.get("direction"), {"x": 0.0, "y": -1.0})
			out["spread"] = float(raw.get("spread", 45.0))
			out["velocity"] = float(raw.get("velocity", 40.0))
			out["gravity"] = to_vec2(raw.get("gravity"), {"x": 0.0, "y": 20.0})
			out["scale_min"] = float(raw.get("scale_min", 0.4))
			out["scale_max"] = float(raw.get("scale_max", 1.0))
	return out

static func to_color(v, fallback: Color) -> Color:
	if v is Color:
		return v
	if v is String:
		var s: String = v.trim_prefix("#")
		if s.length() == 6 and s.is_valid_hex_number(false):
			return Color.html(s)
	return fallback

static func to_vec2(v, fallback: Dictionary) -> Dictionary:
	if v is Vector2:
		return {"x": v.x, "y": v.y}
	if v is Dictionary:
		return {"x": float(v.get("x", fallback["x"])), "y": float(v.get("y", fallback["y"]))}
	return {"x": float(fallback["x"]), "y": float(fallback["y"])}

static func as_vec2(v: Dictionary) -> Vector2:
	return Vector2(float(v.get("x", 0.0)), float(v.get("y", 0.0)))
