extends RefCounted
## 单个图层。用 Dictionary 承载（不是 class），因为 JSON 是文档的真源，
## 保持"文档 <-> 字典"零阻抗比类型安全更重要。
##
## 图层是树：`group`（容器 / 画布）可以有 children，子图层的位置尺寸都是
## **相对父容器**的像素（见 doc/frame.gd）。所以移动/缩放容器会带着内容一起走。
##
## 遮罩与滤镜**不是图层属性，而是独立的效果图层**（type = mask / filter）：
## 它们和普通图层并列在同一条兄弟列表里，靠 `cover` 决定覆盖**它下方**的哪几层。
##   · mask   —— 形状遮罩：把它覆盖的若干层合成后整体乘 alpha；
##   · filter —— 滤镜：对覆盖的若干层做调色/模糊/暗角/色调分离/阴影/发光/描边。
## 因此同一个元素可以既被遮罩又被调色，也可以被另一个效果层再包一层 —— 顺序就是图层次序。
##
## 几何：遮罩层的 `transform` 就是遮罩形状的框；滤镜层平时没有几何，
## 但可以打开 `region`（作用区域）—— 那时它的 `transform` 就是"滤镜只在框内生效"的框，
## 形状/圆角和遮罩同一套。两种效果层因此都能在画布上直接拖/缩放。

const StudioPaths = preload("res://src/doc/paths.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")
const Frame = preload("res://src/doc/frame.gd")
const Timeline = preload("res://src/doc/timeline.gd")

const TYPES := ["color", "image", "text", "particles", "group", "mask", "filter"]
const BLENDS := ["mix", "add", "sub", "mul", "screen", "overlay"]
## 效果图层：本身不画东西，作用在下方被覆盖的图层上。
const EFFECT_TYPES := ["mask", "filter"]
## cover.mode：
##   "all"   —— 下方全部图层；
##   "count" —— 只覆盖下方紧邻的 count 层；
##   "range" —— 覆盖"从效果层往下数"的第 from ~ to 层（1 = 紧邻它的下一层，闭区间）；
##   "layer" —— **只覆盖指定的那一层**（按图层 id 绑定，`cover.target`，限同一组兄弟）。
## 前三种用"离效果层多远"表达，效果层移动时覆盖的是同样的那几层；"layer" 与位置无关：
## 目标层上下移动、中间插入别的图层都不影响绑定，效果层自己放在哪儿都一样。
const COVER_MODES := ["all", "count", "range", "layer"]
## cover.count 的上限。只是防手滑，实际覆盖层数受兄弟数量限制。
const MAX_COVER := 256


static func _base(type: String, layer_name: String) -> Dictionary:
	return {
		"id": "l_" + StudioPaths.new_pack_id().substr(0, 8),
		"type": type,
		"name": layer_name,
		"visible": true,
		# w / h = 0 表示"还没定过尺寸"，语义是铺满父级（根层就是铺满画布）。
		# 遮罩层用同一个框表达"遮罩形状的位置与大小"。
		"transform": {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0,
				"scale": 1.0, "rotation": 0.0, "opacity": 1.0},
		"blend": "mix",
		"tracks": [],
		"effects": [],
		"script_path": "",
		"shader_path": "",
	}


static func is_group(layer: Dictionary) -> bool:
	return String(layer.get("type", "")) == "group"


static func is_effect(layer: Dictionary) -> bool:
	return String(layer.get("type", "")) in EFFECT_TYPES


static func is_mask(layer: Dictionary) -> bool:
	return String(layer.get("type", "")) == "mask"


static func is_filter(layer: Dictionary) -> bool:
	return String(layer.get("type", "")) == "filter"


## 是否是有自己节点几何的普通图层（效果层不参与框/选区）。
static func is_content(layer: Dictionary) -> bool:
	return not layer.is_empty() and not is_effect(layer)


static func make_color(layer_name: String, color: Color) -> Dictionary:
	var l := _base("color", layer_name)
	l["color"] = color
	return l


static func make_image(layer_name: String, src: String) -> Dictionary:
	var l := _base("image", layer_name)
	l["src"] = src
	l["fit"] = "contain"
	l["speed"] = 1.0
	# 帧序列（动图）：非空时优先于 src，逐帧播放（见 LayerNodes.DSTK_IMAGE_SCRIPT）。
	l["frames"] = []
	l["fps"] = 12.0
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


## 容器 / 画布：一个可以嵌套的框，子图层的位置相对它。clip = 裁掉超出的部分。
static func make_group(layer_name: String, w: float = 0.0, h: float = 0.0) -> Dictionary:
	var l := _base("group", layer_name)
	l["clip"] = true
	l["children"] = []
	if w > 0.0 and h > 0.0:
		l["transform"]["w"] = w
		l["transform"]["h"] = h
	return l


## 遮罩层：形状 + 边框（边框 = transform 的 x/y/w/h，w/h = 0 表示覆盖整个父级）。
static func make_mask(layer_name: String) -> Dictionary:
	var l := _base("mask", layer_name)
	l["cover"] = normalize_cover(null)
	l.merge(Mask.defaults())
	return l


## 滤镜层：7 组滤镜参数（默认全关；勾选任意一组才生效）+ 作用区域。
## 作用区域默认关闭 = 作用在被覆盖图层的整个区域上；打开后这一层就有了自己的框
## （位置/尺寸/形状），只有框内是滤镜后的样子 —— 框的几何同样存在 transform 里。
static func make_filter(layer_name: String) -> Dictionary:
	var l := _base("filter", layer_name)
	l["cover"] = normalize_cover(null)
	l["filters"] = Filters.defaults()
	l["region"] = Filters.region_defaults()
	return l


## 覆盖下方多少层：0 = 下方全部，>0 = 只覆盖紧邻的 N 层。
## range 模式不返回具体层数（取决于下方有几层），用 cover_range / doc.covered_count。
static func cover_count(layer: Dictionary) -> int:
	var cover = layer.get("cover")
	if not (cover is Dictionary):
		return 0
	if String(cover.get("mode", "all")) != "count":
		return 0
	return clampi(int(cover.get("count", 1)), 1, MAX_COVER)


## 覆盖区间（闭区间），坐标是"从效果层往下数第几层"（1 = 紧邻下方）。
## below = 下方可覆盖的层数。y < x 表示空区间（下方没东西 / from 超出）。
## **场景构造与界面显示都必须只走这一个函数**，否则"显示覆盖 2 层"和实际画出来的层数会不一致。
static func cover_range(layer: Dictionary, below: int) -> Vector2i:
	var n := maxi(0, below)
	if n == 0:
		return Vector2i(1, 0)
	var cover = layer.get("cover")
	if not (cover is Dictionary):
		return Vector2i(1, n)
	match String(cover.get("mode", "all")):
		"layer":
			# 绑定模式不走"离我多远"这套编号，覆盖哪一层由 cover_target_path 解析。
			return Vector2i(1, 0)
		"count":
			return Vector2i(1, clampi(int(cover.get("count", 1)), 1, n))
		"range":
			var a := clampi(int(cover.get("from", 1)), 1, MAX_COVER)
			if a > n:
				return Vector2i(1, 0)
			return Vector2i(a, mini(maxi(a, int(cover.get("to", a))), n))
		_:
			return Vector2i(1, n)


## 覆盖范围的简短描述（图层列表与属性面板共用）。按设置原样显示，不代表实际层数。
static func cover_label(layer: Dictionary) -> String:
	var cover = layer.get("cover")
	if not (cover is Dictionary):
		return "下方全部"
	match String(cover.get("mode", "all")):
		"layer":
			return "指定图层"
		"count":
			return "下方 %d 层" % clampi(int(cover.get("count", 1)), 1, MAX_COVER)
		"range":
			var a := clampi(int(cover.get("from", 1)), 1, MAX_COVER)
			var b := maxi(a, clampi(int(cover.get("to", a)), 1, MAX_COVER))
			return "第 %d~%d 层" % [a, b]
		_:
			return "下方全部"


## 绑定模式的目标图层 id（不是绑定模式 / 没设目标时为空串）。
static func cover_target(layer: Dictionary) -> String:
	var cover = layer.get("cover")
	if not (cover is Dictionary):
		return ""
	if String(cover.get("mode", "all")) != "layer":
		return ""
	return String(cover.get("target", ""))


## 一组图层（`[{layer: …}, …]`）在**父级像素**里的包围盒。
## 效果层没有自己的内容框（遮罩框是形状、滤镜层只有可选的作用区域），跳过；
## 一个能算的都没有时返回 Rect2()（size 为 0），调用方自己决定退化成什么。
## 暗角的坐标空间、打开「限制作用区域」时框的初始值都走这一份，别各算各的。
static func bounds_of(items: Array, parent_size: Vector2) -> Rect2:
	var r := Rect2()
	var first := true
	for item in items:
		if not (item is Dictionary):
			continue
		var layer: Dictionary = item.get("layer", {})
		if layer.is_empty() or is_effect(layer):
			continue
		var fr := Frame.of(layer, parent_size)
		r = fr if first else r.merge(fr)
		first = false
	return r


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
		# 关键帧顺序/取值在这里一次洗好，后面的采样与场景构造就不用再防错。
		out["tracks"] = Timeline.normalize_tracks(raw["tracks"])
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
			var frames := []
			if raw.get("frames") is Array:
				for f in raw["frames"]:
					if f is String and not String(f).is_empty():
						frames.append(String(f))
			out["frames"] = frames
			out["fps"] = clampf(float(raw.get("fps", 12.0)), 0.5, 60.0)
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
		"group":
			out["clip"] = bool(raw.get("clip", true))
			var kids := []
			if raw.get("children") is Array:
				for item in raw["children"]:
					if item is Dictionary:
						kids.append(normalize(item))
			out["children"] = kids
		"mask":
			out["cover"] = normalize_cover(raw.get("cover"))
			out.merge(Mask.normalize(raw))
		"filter":
			out["cover"] = normalize_cover(raw.get("cover"))
			out["filters"] = Filters.normalize(raw.get("filters"))
			out["region"] = Filters.normalize_region(raw.get("region"))
	return out


static func normalize_cover(raw) -> Dictionary:
	var out := {"mode": "all", "count": 1, "from": 1, "to": 1, "target": ""}
	if not (raw is Dictionary):
		return out
	var mode := String(raw.get("mode", "all"))
	out["mode"] = mode if mode in COVER_MODES else "all"
	out["count"] = clampi(int(raw.get("count", 1)), 1, MAX_COVER)
	out["from"] = clampi(int(raw.get("from", 1)), 1, MAX_COVER)
	out["to"] = maxi(int(out["from"]), clampi(int(raw.get("to", out["from"])), 1, MAX_COVER))
	out["target"] = String(raw.get("target", ""))
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
