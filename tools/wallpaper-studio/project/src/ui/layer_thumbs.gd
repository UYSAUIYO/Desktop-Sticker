extends RefCounted
## 图层列表每一行的**缩略图**。
##
## 为什么要它：全是文字的列表里，"哪一行是哪张图"只能靠名字猜。图片层直接给
## 资源缩略图，其余类型给一眼能认的符号图标。
##
## 图标是 24×24 的 Image 现画的（不引外部资源：编辑器的工程目录里没有图标文件，
## 也不该为了几个图标往 res:// 里塞二进制）。画完按 key 缓存 —— refresh 每次重建
## 整个列表，不缓存的话每刷一次就重画一遍所有图标。
##
## 画圆 / 渐变用逐像素：Image 只有 fill_rect / blend_rect 这些矩形原语。

const Layer = preload("res://src/doc/layer.gd")

const SIZE := 24
const BACK := Color(0.15, 0.16, 0.19)
const LINE := Color(0.52, 0.55, 0.62)
const INK := Color(0.86, 0.89, 0.94)

var _cache: Dictionary = {}


func icon_for(layer: Dictionary, project_dir: String) -> Texture2D:
	var key := _key(layer)
	if _cache.has(key):
		return _cache[key] as Texture2D
	var img := _draw(layer, project_dir)
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


func _key(layer: Dictionary) -> String:
	var type := String(layer.get("type", "color"))
	match type:
		"color":
			return "color:" + String(Layer.to_color(layer.get("color"), Color.BLACK).to_html(false))
		"image":
			return "image:" + String(layer.get("src", ""))
	return type


func _draw(layer: Dictionary, project_dir: String) -> Image:
	var img := _blank()
	match String(layer.get("type", "color")):
		"color":
			img.fill(Layer.to_color(layer.get("color"), Color.BLACK))
		"image":
			_draw_image(img, String(layer.get("src", "")), project_dir)
		"text":
			_draw_text(img)
		"particles":
			_draw_particles(img)
		"group":
			_draw_group(img)
		"mask":
			_draw_mask(img)
		"filter":
			_draw_filter(img)
	_draw_border(img)
	return img


func _blank() -> Image:
	var img := Image.create(SIZE, SIZE, false, Image.FORMAT_RGBA8)
	img.fill(BACK)
	return img


func _draw_border(img: Image) -> void:
	img.fill_rect(Rect2i(0, 0, SIZE, 1), LINE)
	img.fill_rect(Rect2i(0, SIZE - 1, SIZE, 1), LINE)
	img.fill_rect(Rect2i(0, 0, 1, SIZE), LINE)
	img.fill_rect(Rect2i(SIZE - 1, 0, 1, SIZE), LINE)


func _draw_image(img: Image, src: String, project_dir: String) -> void:
	if src.is_empty() or project_dir.is_empty():
		_draw_text(img)
		return
	var path := project_dir.path_join(src)
	if not FileAccess.file_exists(path):
		# 资源丢了：画个叉，和"还没选图"区分开（列表里这层会被标红，图标也该说点什么）。
		for i in SIZE:
			img.set_pixel(i, i, Color(0.85, 0.35, 0.32))
			img.set_pixel(SIZE - 1 - i, i, Color(0.85, 0.35, 0.32))
		return
	var src_img := Image.new()
	if src_img.load(path) != OK:
		return
	src_img.resize(SIZE, SIZE, Image.INTERPOLATE_LANCZOS)
	src_img.convert(Image.FORMAT_RGBA8)
	img.blend_rect(src_img, Rect2i(0, 0, SIZE, SIZE), Vector2i.ZERO)


func _draw_text(img: Image) -> void:
	# 一个 "T"：横条在上，竖条居中。
	img.fill_rect(Rect2i(6, 6, 12, 3), INK)
	img.fill_rect(Rect2i(10, 6, 3, 12), INK)


func _draw_particles(img: Image) -> void:
	var spots := [Vector2i(6, 7), Vector2i(16, 5), Vector2i(11, 12),
			Vector2i(7, 17), Vector2i(17, 15)]
	for p in spots:
		img.fill_rect(Rect2i(p.x, p.y, 3, 3), INK)


func _draw_group(img: Image) -> void:
	for i in range(2, SIZE - 2):
		img.set_pixel(i, 4, LINE)
		img.set_pixel(i, SIZE - 5, LINE)
		img.set_pixel(4, i, LINE)
		img.set_pixel(SIZE - 5, i, LINE)
	img.fill_rect(Rect2i(9, 9, 6, 6), Color(0.42, 0.62, 0.95, 0.85))


func _draw_mask(img: Image) -> void:
	# 中心亮、边缘透明的圆：遮罩就是"中间留、边上挖掉"。
	var c := Vector2(SIZE * 0.5, SIZE * 0.5)
	for y in SIZE:
		for x in SIZE:
			var d := Vector2(x + 0.5, y + 0.5).distance_to(c)
			var a := clampf(1.0 - (d - 4.0) / 6.0, 0.0, 1.0)
			img.set_pixel(x, y, Color(0.96, 0.72, 0.24, a))


func _draw_filter(img: Image) -> void:
	# 三条横线：滤镜 = 对每一行像素做同样的事。
	for i in [6, 12, 18]:
		img.fill_rect(Rect2i(5, i, 14, 2), Color(0.36, 0.85, 0.62))
