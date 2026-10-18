extends RefCounted
## 遮罩层（mask）的形状参数。**边框不在这个文件里** —— 遮罩的位置与大小就是
## 效果图层自己的 `transform`（x/y/w/h，w/h = 0 表示覆盖整个父级），
## 所以遮罩在画布上可以直接拖动/缩放，和普通元素共用同一套选区框。
##
## 实现方式仍是**着色器里的 alpha 衰减**，不是把内容裁成位图：
##   · 坐标用「父级 UV 比例」表达，缩放窗口时遮罩跟着一起缩放，不会糊；
##   · 对文字/矢量内容同样有效（只乘 alpha，不重新采样内容自己的贴图）；
##   · 羽化 = 边缘的 smoothstep，和分辨率无关。
## 遮罩层作用于**它下方被覆盖的若干层合成后的结果**（见 scene_builder 的合成节点），
## 不是某个图层自己的属性。

const Frame = preload("res://src/doc/frame.gd")

const SHAPES := ["rect", "rounded", "ellipse", "image"]
## 图片遮罩内联进场景前会先缩到这么大：遮罩本来就会被羽化/缩放，480 足够。
const IMAGE_MAX := 480


static func defaults() -> Dictionary:
	return {
		"shape": "rect",
		"radius": 24.0,
		"feather": 0.0,
		"invert": false,
		"src": "",
	}


static func normalize(raw) -> Dictionary:
	var out := defaults()
	if not (raw is Dictionary):
		return out
	var shape := String(raw.get("shape", "rect"))
	out["shape"] = shape if shape in SHAPES else "rect"
	out["radius"] = maxf(0.0, float(raw.get("radius", out["radius"])))
	out["feather"] = maxf(0.0, float(raw.get("feather", out["feather"])))
	out["invert"] = bool(raw.get("invert", false))
	out["src"] = String(raw.get("src", ""))
	return out


## 形状编号，直接进着色器。
static func shape_code(mask_layer: Dictionary) -> int:
	return code_of(String(mask_layer.get("shape", "rect")))


## 形状字符串 -> 着色器里的形状编号。
## 滤镜层的「作用区域」也走这一份：两边共用 shape_alpha，编号必须一致。
static func code_of(shape: String) -> int:
	match shape:
		"rounded":
			return 1
		"ellipse":
			return 2
		"image":
			return 3
		_:
			return 0


## 遮罩框在**父级 UV**（0..1）里的矩形。合成节点渲染到父级大小的 SubViewport 里，
## 着色器拿到的同样是一份 0..1 的 UV，两边直接对齐 —— 不需要再乘图层比例。
static func uv_rect(mask_layer: Dictionary, parent_size: Vector2) -> Rect2:
	var r := Frame.of(mask_layer, parent_size)
	var pw := maxf(1.0, float(parent_size.x))
	var ph := maxf(1.0, float(parent_size.y))
	return Rect2(Vector2(r.position.x / pw, r.position.y / ph),
			Vector2(r.size.x / pw, r.size.y / ph))


## 图片遮罩的贴图：载入后缩到 IMAGE_MAX 以内再交给着色器采样。
## 找不到文件返回 null（调用方按 alpha=0 处理，画面上就是覆盖区域被整块遮住 ——
## 明确可见的错，比"当没设遮罩"更容易发现资源丢了）。
static func load_texture(mask_layer: Dictionary, project_dir: String) -> Texture2D:
	var rel := String(mask_layer.get("src", ""))
	if rel.is_empty():
		return null
	var path := rel
	if not rel.contains("://"):
		if project_dir.is_empty():
			return null
		path = project_dir.path_join(rel)
		# 导出时资源被平铺拷进 assets/，工程里的 "assets/x.png" 会变成 "<dir>/x.png"。
		if not FileAccess.file_exists(path):
			path = project_dir.path_join(rel.get_file())
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
