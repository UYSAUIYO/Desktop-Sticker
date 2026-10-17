extends RefCounted
## 每种图层类型 -> 节点。只构造，不落盘（落盘是 exporter 的事）。

const Layer = preload("res://src/doc/layer.gd")

## 时钟/日期图层的运行时脚本。
##
## 预览与导出共用这**同一份源码**：预览时在内存里编译成 GDScript 挂上去，
## 导出时 PackedScene 会把它作为 sub_resource 写进场景。
## 所以不存在"预览用的那份"和"导出用的那份"两个文件可能不同步的问题。
##
## 这是 Studio 生成的脚本之一；它不含任何 IO/网络，是纯展示逻辑，
## 因此不计入 manifest 的 content_flags。
const DSTK_CLOCK_SCRIPT := "extends Label

@export var mode: String = \"clock\"
@export var date_format: String = \"%04d-%02d-%02d\"

func _ready() -> void:
	_refresh()

func _process(_delta: float) -> void:
	_refresh()

func _refresh() -> void:
	var now := Time.get_datetime_dict_from_system()
	if mode == \"date\":
		text = date_format % [now.year, now.month, now.day]
	else:
		text = \"%02d:%02d:%02d\" % [now.hour, now.minute, now.second]
"

## 图片图层的运行时解码脚本 —— **只在导出时用**。
##
## 为什么需要它：template_release 运行库里 ResourceLoader **不认识裸 .png**
## （只有编辑器导入产出的 .ctex 能加载），所以贴图没办法做成 ext_resource 引用。
## 而把像素内联进场景的代价是"未压缩的 RGBA"：一张 4K 背景 PNG 约 6 MB，
## 内联后是 33 MB。所以导出时不内联，改由这个脚本在运行时读包内的原始文件解码。
##
## 代价是每张图多一次解码（毫秒级），换来包体积基本等于源素材体积。
## 预览不用它 —— 预览直接把内存纹理赋给节点，没有解码开销。
const DSTK_IMAGE_SCRIPT := "extends TextureRect

@export var src: String = \"\"

func _ready() -> void:
	_apply()

func _apply() -> void:
	if src.is_empty():
		return
	var bytes := FileAccess.get_file_as_bytes(src)
	if bytes.is_empty():
		push_warning(\"dstk: cannot read \" + src)
		return
	var img := Image.new()
	var err := FAILED
	match src.get_extension().to_lower():
		\"png\":
			err = img.load_png_from_buffer(bytes)
		\"jpg\", \"jpeg\":
			err = img.load_jpg_from_buffer(bytes)
		\"webp\":
			err = img.load_webp_from_buffer(bytes)
		\"bmp\":
			err = img.load_bmp_from_buffer(bytes)
		\"tga\":
			err = img.load_tga_from_buffer(bytes)
		_:
			err = img.load_png_from_buffer(bytes)
	if err != OK:
		push_warning(\"dstk: cannot decode \" + src)
		return
	texture = ImageTexture.create_from_image(img)
"


## 返回一个以 layer 内容为准的 Control（粒子也包一层 Control，便于统一布局与变换）。
static func build(layer: Dictionary, project_dir: String) -> Control:
	match String(layer.get("type", "color")):
		"image":
			return _build_image(layer, project_dir)
		"text":
			return _build_text(layer)
		"particles":
			return _build_particles(layer, project_dir)
		_:
			return _build_color(layer)


static func _build_color(layer: Dictionary) -> Control:
	var n := ColorRect.new()
	n.color = Layer.to_color(layer.get("color"), Color.BLACK)
	n.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return n


static func _build_image(layer: Dictionary, project_dir: String) -> Control:
	var n := TextureRect.new()
	n.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	n.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	match String(layer.get("fit", "contain")):
		"fill":
			n.stretch_mode = TextureRect.STRETCH_SCALE
		"cover":
			n.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
		"tile":
			n.stretch_mode = TextureRect.STRETCH_TILE
		_:
			n.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED

	var rel := String(layer.get("src", ""))
	if not rel.is_empty() and project_dir.begins_with("res://"):
		# 导出路径：不内联像素，挂运行时解码脚本（见 DSTK_IMAGE_SCRIPT 的说明）。
		var script = _compile(DSTK_IMAGE_SCRIPT)
		if script != null:
			n.set_script(script)
			n.set("src", project_dir.path_join(rel.get_file()))
			return n
	# 预览路径（或导出时脚本编译失败）：直接用内存纹理。
	n.texture = load_texture(rel, project_dir)
	return n


static func _build_text(layer: Dictionary) -> Control:
	var n := Label.new()
	n.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	n.text = String(layer.get("text", ""))
	n.add_theme_font_size_override("font_size", int(layer.get("font_size", 64)))
	n.add_theme_color_override("font_color", Layer.to_color(layer.get("color"), Color.WHITE))
	match String(layer.get("align", "center")):
		"left":
			n.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
		"right":
			n.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		_:
			n.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	n.vertical_alignment = VERTICAL_ALIGNMENT_CENTER

	var dynamic := String(layer.get("dynamic", "none"))
	if dynamic != "none":
		var script = _compile(DSTK_CLOCK_SCRIPT)
		if script != null:
			n.set_script(script)
			n.set("mode", dynamic)
			n.set("date_format", String(layer.get("date_format", "%04d-%02d-%02d")))
	return n


static func _build_particles(layer: Dictionary, project_dir: String) -> Control:
	# GPUParticles2D 是 Node2D，不是 Control；用一个铺满的 Control 包住以统一布局。
	var wrap := Control.new()
	wrap.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	wrap.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var n := GPUParticles2D.new()
	n.amount = maxi(1, int(layer.get("amount", 120)))
	n.lifetime = maxf(0.1, float(layer.get("lifetime", 6.0)))
	n.emitting = true
	# 必须放大可见矩形：GPUParticles2D 默认只画 (-100,-100)~200x200 这一块，
	# 粒子一飞出这个小框就被裁掉，画面上看起来"一个粒子都没有"（实测踩过）。
	n.visibility_rect = Rect2(Vector2(-2048, -2048), Vector2(4096, 4096))
	# 发射点放在画布底部中央。默认在 (0,0) 且默认方向朝上，粒子会直接从左上角
	# 飞出屏幕外 —— 新加的粒子图层看起来什么都没发生。
	var place := func() -> void:
		n.position = Vector2(wrap.size.x * 0.5, wrap.size.y * 0.85)
	wrap.resized.connect(place)
	# 粒子贴图通常很小（几十 KB），直接内联；大图才值得走运行时解码。
	var tex := load_texture(String(layer.get("texture", "")), project_dir)
	n.texture = tex if tex != null else default_particle_texture()
	n.process_material = _particle_material(layer)
	wrap.add_child(n)
	place.call()
	return wrap


## 没选贴图时的内置圆点：白色、边缘柔和。没有它的话新加的粒子图层在画布上
## 完全看不见，用户会以为"添加失败"。导出时这张小图会被内联进场景（几 KB）。
static func default_particle_texture() -> Texture2D:
	var size := 32
	var img := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var center := Vector2(size * 0.5, size * 0.5)
	var radius := size * 0.5
	for y in size:
		for x in size:
			var d := Vector2(x + 0.5, y + 0.5).distance_to(center) / radius
			var a := clampf(1.0 - d, 0.0, 1.0)
			img.set_pixel(x, y, Color(1, 1, 1, a * a))
	return ImageTexture.create_from_image(img)


static func _particle_material(layer: Dictionary) -> ParticleProcessMaterial:
	var mat := ParticleProcessMaterial.new()
	var dir = Layer.as_vec2(layer.get("direction", Layer.to_vec2(null, {"x": 0.0, "y": -1.0})))
	mat.direction = Vector3(dir.x, dir.y, 0.0)
	mat.spread = float(layer.get("spread", 45.0))
	var velocity := float(layer.get("velocity", 40.0))
	mat.initial_velocity_min = velocity * 0.8
	mat.initial_velocity_max = velocity
	var g = Layer.as_vec2(layer.get("gravity", Layer.to_vec2(null, {"x": 0.0, "y": 20.0})))
	mat.gravity = Vector3(g.x, g.y, 0.0)
	mat.scale_min = float(layer.get("scale_min", 0.4))
	mat.scale_max = float(layer.get("scale_max", 1.0))
	return mat


## 把一段源码常量编译成 GDScript。编译失败返回 null，调用方退回"内联纹理"路径。
static func _compile(source: String):
	var script := GDScript.new()
	script.source_code = source
	if script.reload() != OK:
		push_warning("dstk: generated script failed to compile")
		return null
	return script


## 贴图加载。
##   "res://…"  —— 导出时不该走到这里（导出走 DSTK_IMAGE_SCRIPT）；预览时若拿到
##                 res:// 路径，用 load()（要求路径确实是一个可加载资源）。
##   其它        —— 预览：按绝对/相对文件路径读像素建内存纹理。
static func load_texture(rel: String, project_dir: String) -> Texture2D:
	if rel.is_empty():
		return null
	if rel.begins_with("res://"):
		if not ResourceLoader.exists(rel):
			return null
		return load(rel) as Texture2D
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
	return ImageTexture.create_from_image(img)
