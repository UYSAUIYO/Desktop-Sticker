extends RefCounted
## 每种图层类型 -> 节点。只构造，不落盘（落盘是 exporter 的事）。
##
## 一个图层最终的样子（由 scene_builder 组装）：
##
##   layer_box（锚点 = 框，旋转/不透明度/可见性）
##     └ 模糊链（可选，两趟 SubViewport）
##          └ 阴影 / 描边 / 发光 副本（可选，染色副本垫在下面）
##               └ 内容（纯色 / 图片 / 文字 / 粒子 / 容器子层）
##
## 遮罩与色彩类滤镜是**着色器**（见 doc/filters.gd）：遮罩吃的是画布 UV 比例，
## 所以不栅格化、不糊、对文字同样有效。

const Layer = preload("res://src/doc/layer.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")

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


## 内容节点：铺满自己的父级（= 图层框的内部）。粒子也包一层 Control，便于统一布局。
static func build_content(layer: Dictionary, project_dir: String) -> Control:
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
	# 显式框（拖过尺寸）的文本框要按框宽折行：拖窄了还溢出到框外就白改了。
	# 没拖过尺寸的老图层保持不折行 —— 那是既有文档的样子，不能悄悄变。
	if float(layer.get("transform", {}).get("w", 0.0)) > 0.0:
		n.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

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
	var px := 32
	var img := Image.create(px, px, false, Image.FORMAT_RGBA8)
	var center := Vector2(px * 0.5, px * 0.5)
	var radius := px * 0.5
	for y in px:
		for x in px:
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


# ---------------------------------------------------------------- 遮罩 / 滤镜

## 这一层需要的材质：遮罩或色彩类滤镜 -> 着色器；只有混合模式 -> CanvasItemMaterial。
## uv = 图层框在画布里的比例矩形；size = 图层本地像素尺寸。
## 返回 null 表示"什么都不用挂"。
static func style_material(layer: Dictionary, uv: Rect2, size: Vector2,
		project_dir: String) -> Material:
	var f: Dictionary = layer.get("filters", {})
	var mask: Dictionary = layer.get("mask", {})
	var mask_code := Mask.shape_code(mask)
	var grade := Filters.needs_material(f)
	var blend := String(layer.get("blend", "mix"))
	if mask_code < 0 and not grade:
		return blend_material(blend)

	var shader := Shader.new()
	shader.code = Filters.layer_shader(blend)
	var mat := ShaderMaterial.new()
	mat.shader = shader
	var uniforms := Filters.grade_uniforms(f, uv)
	uniforms["screen_origin"] = Vector2.ZERO
	uniforms["screen_size"] = Vector2.ONE
	if mask_code >= 0:
		var mr := Mask.uv_rect(mask, uv, size)
		var msize := Mask.rect(mask, size).size
		var radius := float(mask.get("radius", 0.0))
		var feather := float(mask.get("feather", 0.0))
		uniforms["mask_shape"] = mask_code
		# 几何一律压成 Vector4：set_shader_parameter 收到 Rect2 会直接崩。
		uniforms["mask_rect"] = Vector4(mr.position.x, mr.position.y, mr.size.x, mr.size.y)
		uniforms["mask_radii"] = Vector2(
				clampf(radius / maxf(1.0, msize.x), 0.0, 0.5),
				clampf(radius / maxf(1.0, msize.y), 0.0, 0.5))
		uniforms["mask_feather"] = Vector2(
				feather / maxf(1.0, msize.x), feather / maxf(1.0, msize.y))
		uniforms["mask_invert"] = bool(mask.get("invert", false))
		if mask_code == 3:
			var tex := Mask.load_texture(mask, project_dir)
			if tex == null:
				# 遮罩图丢了就整层遮住：明显可见的错误，比"当没设遮罩"更难漏掉。
				var blank := Image.create(1, 1, false, Image.FORMAT_RGBA8)
				blank.fill(Color(0, 0, 0, 0))
				tex = ImageTexture.create_from_image(blank)
			uniforms["mask_tex"] = tex
	for key in uniforms.keys():
		mat.set_shader_parameter(key, uniforms[key])
	return mat


static func blend_material(blend: String) -> Material:
	if blend == "mix":
		return null
	var mat := CanvasItemMaterial.new()
	match blend:
		"add":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
		"sub":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_SUB
		"mul":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_MUL
		_:
			# screen / overlay 等阶段 3 用自定义着色器实现，现在先当 mix 处理。
			return null
	return mat


## 把材质挂到**这棵子树里所有会画东西的节点**上（含 root 自己）。
## 粒子有自己的绘制着色器，挂 canvas_item 材质会盖掉它，所以跳过。
## 注意 root 自己也要挂：纯色/图片/文字的内容节点就是叶子，漏掉它等于整层没生效
## （踩过：只遍历 children，结果只有容器里的第二层才吃到滤镜）。
static func apply_material(root: Node, mat: Material) -> void:
	if mat == null:
		return
	if root is CanvasItem and not (root is GPUParticles2D):
		(root as CanvasItem).material = mat
	for child in root.get_children():
		if child is GPUParticles2D:
			continue
		apply_material(child, mat)


## 阴影 / 描边 / 发光：复制内容、染色、垫在下面。返回按绘制顺序排好的副本数组。
## 粒子与容器不做副本（复制粒子会重开一套发射器；复制容器等于复制整棵子树）。
static func effect_copies(layer: Dictionary, content: Control, size: Vector2) -> Array:
	var f: Dictionary = layer.get("filters", {})
	var out := []
	if not Filters.needs_copy(f):
		return out
	if String(layer.get("type", "")) == "particles" or Layer.is_group(layer):
		return out

	if Filters.on(f, Filters.SHADOW):
		var g: Dictionary = f[Filters.SHADOW]
		out.append(_copy_tinted(content, g.get("color"), float(g.get("alpha", 0.45)),
				Vector2(float(g.get("x", 0.0)), float(g.get("y", 0.0)))))
	if Filters.on(f, Filters.STROKE):
		var s: Dictionary = f[Filters.STROKE]
		var w := float(s.get("width", 3.0))
		if w > 0.0:
			# 八方向各一份，凑出描边；半径大时会有轻微"重影"，但不需要额外渲染趟。
			for dir in [Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1),
					Vector2(0.707, 0.707), Vector2(-0.707, 0.707),
					Vector2(0.707, -0.707), Vector2(-0.707, -0.707)]:
				out.append(_copy_tinted(content, s.get("color"), 1.0, dir * w))
	if Filters.on(f, Filters.GLOW):
		var g2: Dictionary = f[Filters.GLOW]
		var radius := float(g2.get("radius", 20.0))
		var copy := _copy_tinted(content, g2.get("color"), float(g2.get("alpha", 0.55)),
				Vector2.ZERO)
		out.append(wrap_blur(copy, radius, size) if radius > 0.0 else copy)
	return out


static func _copy_tinted(content: Control, color, alpha: float, offset: Vector2) -> Control:
	var dup: Control = content.duplicate()
	var c := Layer.to_color(color, Color.BLACK)
	dup.modulate = Color(c.r, c.g, c.b, clampf(alpha, 0.0, 1.0))
	# 内容节点是铺满父级的锚点节点：位移只能写 offsets，写 position 会被布局覆盖。
	dup.offset_left += offset.x
	dup.offset_top += offset.y
	dup.offset_right += offset.x
	dup.offset_bottom += offset.y
	dup.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return dup


## 两趟可分离高斯（水平 + 垂直），每趟一个 SubViewport。
## 半径以**图层宽高的比例**传给着色器，所以窗口放大时视觉强度不变（不会糊成一团）。
## stretch = true：SubViewport 尺寸跟随容器，窗口越大渲染分辨率越高。
static func wrap_blur(content: Control, radius: float, size: Vector2) -> Control:
	var px := Vector2i(maxi(1, int(size.x)), maxi(1, int(size.y)))

	var inner_vp := SubViewport.new()
	inner_vp.size = px
	inner_vp.transparent_bg = true
	inner_vp.disable_3d = true
	inner_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	inner_vp.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS

	var inner := SubViewportContainer.new()
	inner.stretch = true
	inner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	inner.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	inner.add_child(inner_vp)
	inner.material = _blur_material(radius, size, true)

	var outer_vp := SubViewport.new()
	outer_vp.size = px
	outer_vp.transparent_bg = true
	outer_vp.disable_3d = true
	outer_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	outer_vp.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
	outer_vp.add_child(inner)

	var outer := SubViewportContainer.new()
	outer.stretch = true
	outer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	outer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	outer.add_child(outer_vp)
	outer.material = _blur_material(radius, size, false)

	# 内容进内层 viewport：锚点相对 viewport 尺寸，铺满。
	content.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	content.offset_left = 0.0
	content.offset_top = 0.0
	content.offset_right = 0.0
	content.offset_bottom = 0.0
	inner_vp.add_child(content)
	return outer


static func _blur_material(radius: float, size: Vector2, horizontal: bool) -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = Filters.blur_shader()
	var mat := ShaderMaterial.new()
	mat.shader = shader
	var step := Vector2.ZERO
	# 一趟只走一个方向；7 个采样点跨度约 6 个步长，所以 step 取半径的 1/6。
	var unit := Vector2(radius / maxf(1.0, size.x), radius / maxf(1.0, size.y)) / 6.0
	if horizontal:
		step = Vector2(unit.x, 0.0)
	else:
		step = Vector2(0.0, unit.y)
	mat.set_shader_parameter("blur_step", step)
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
