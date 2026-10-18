extends RefCounted
## 遮罩（Mask）：每个图层一个，形状裁掉图层内容的一部分。
##
## 实现方式是**着色器里的 alpha 衰减**，不是把内容裁成位图：
##   · 坐标用「画布 UV 比例」表达，所以缩放窗口时遮罩跟着一起缩放，不会糊；
##   · 对文字/矢量内容同样有效（只乘 alpha，不重新采样内容自己的贴图）；
##   · 羽化 = 边缘的 smoothstep，和分辨率无关。
## 早期设想是用 TextureRect + clip_children，那需要把遮罩栅格化成贴图：
## 每拖一下手柄就要重算几十万像素，还会把贴图塞进导出包。放弃。
##
## 遮罩矩形存的是**图层本地像素**（相对图层左上角），所以图层移动/缩放时遮罩跟着走。

const SHAPES := ["rect", "rounded", "ellipse", "image"]
## 图片遮罩内联进场景前会先缩到这么大：遮罩本来就会被羽化/缩放，480 足够。
const IMAGE_MAX := 480


static func defaults() -> Dictionary:
	return {
		"enabled": false,
		"shape": "rect",
		"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0,
		"radius": 24.0,
		"feather": 0.0,
		"invert": false,
		"src": "",
	}


static func normalize(raw) -> Dictionary:
	var out := defaults()
	if not (raw is Dictionary):
		return out
	out["enabled"] = bool(raw.get("enabled", false))
	var shape := String(raw.get("shape", "rect"))
	out["shape"] = shape if shape in SHAPES else "rect"
	for k in ["x", "y", "w", "h", "radius", "feather"]:
		out[k] = float(raw.get(k, out[k]))
	out["invert"] = bool(raw.get("invert", false))
	out["src"] = String(raw.get("src", ""))
	if float(out["feather"]) < 0.0:
		out["feather"] = 0.0
	if float(out["radius"]) < 0.0:
		out["radius"] = 0.0
	return out


## 遮罩矩形，图层本地像素。宽高为 0 表示"整层"（和选区框的 w/h 同一套语义）。
static func rect(mask: Dictionary, layer_size: Vector2) -> Rect2:
	var w := float(mask.get("w", 0.0))
	var h := float(mask.get("h", 0.0))
	if w <= 0.0 or h <= 0.0:
		return Rect2(Vector2.ZERO, Vector2(maxf(1.0, layer_size.x), maxf(1.0, layer_size.y)))
	return Rect2(Vector2(float(mask.get("x", 0.0)), float(mask.get("y", 0.0))), Vector2(w, h))


## 遮罩矩形换算到画布 UV（0..1）。layer_uv = 图层框在画布里的 UV 矩形。
## 场景构造时算一次写进着色器 uniform —— 运行时窗口怎么变都对得上，因为两边都是比例。
static func uv_rect(mask: Dictionary, layer_uv: Rect2, layer_size: Vector2) -> Rect2:
	var r := rect(mask, layer_size)
	var lw := maxf(1.0, layer_size.x)
	var lh := maxf(1.0, layer_size.y)
	return Rect2(
		layer_uv.position + Vector2(r.position.x / lw, r.position.y / lh) * layer_uv.size,
		Vector2(r.size.x / lw, r.size.y / lh) * layer_uv.size)


## 形状编号，直接进着色器：-1 = 关闭。
static func shape_code(mask: Dictionary) -> int:
	if not bool(mask.get("enabled", false)):
		return -1
	match String(mask.get("shape", "rect")):
		"rounded":
			return 1
		"ellipse":
			return 2
		"image":
			return 3
		_:
			return 0


## 图片遮罩的贴图：载入后缩到 IMAGE_MAX 以内再交给着色器采样。
## 找不到文件返回 null（着色器按 alpha=0 处理，画面上就是整层被遮住 —— 明确可见的错，
## 比"当没设遮罩"更容易发现资源丢了）。
static func load_texture(mask: Dictionary, project_dir: String) -> Texture2D:
	var rel := String(mask.get("src", ""))
	if rel.is_empty():
		return null
	var path := rel
	if not rel.contains("://"):
		if project_dir.is_empty():
			return null
		path = project_dir.path_join(rel)
	if not FileAccess.file_exists(path):
		return null
	var img := Image.new()
	if img.load(path) != OK:
		return null
	var biggest := maxi(img.get_width(), img.get_height())
	if biggest > IMAGE_MAX:
		var k := float(IMAGE_MAX) / float(biggest)
		img.resize(maxi(1, int(img.get_width() * k)), maxi(1, int(img.get_height() * k)),
				Image.INTERPOLATE_BILINEAR)
	if img.get_format() != Image.FORMAT_RGBA8:
		img.convert(Image.FORMAT_RGBA8)
	return ImageTexture.create_from_image(img)
