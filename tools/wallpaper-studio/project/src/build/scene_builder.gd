extends RefCounted
## 文档 -> 完整 Node 树。**预览和导出共用这一条路径**（设计 §2.3 的不变式）。
##
## 图层是树（容器可以嵌套），所以这里也是递归的：每个"内容图层"一个 box，
## box 的锚点 = 它相对父级的框；容器里的子层以容器的框为参照。
## 于是"移动/缩放容器 -> 子元素跟着走"在运行时同样成立（锚点是比例，不是像素）。
##
## 遮罩 / 滤镜是**效果图层**（兄弟节点），不是图层属性。所以每一层子图层的生成是一个
## 从左到右的折叠：普通图层 -> 一个节点进 `built`；遇到效果层 -> 把 `built` 末尾被覆盖
## 的若干节点收进一个合成节点（Stack），再整体套遮罩着色器 / 模糊链，合成节点本身
## 顶替这批图层的位置继续往后排。
##
## 效果层还可以**绑定到指定图层**（cover.mode = "layer"）：这时它不在自己位置上生成任何
## 东西，而是就地包住目标那一层（目标可以在这棵树的任何地方）。绑定表在 build() 里
## 一次算好，往下传给每个兄弟列表，见 add_children。
##
## 合成节点的着色器吃的是**自己贴图的 UV**（0..1 铺满父级），不是 SCREEN_UV ——
## 嵌套容器/嵌套效果时 SCREEN_UV 会跟着外层 viewport 跑，遮罩会错位（见 filters.gd）。

const LayerNodes = preload("res://src/build/layer_nodes.gd")
const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")
const Mask = preload("res://src/doc/mask.gd")

const ROOT_NAME := "WallpaperRoot"
## 图层节点名的前缀 + 图层 id。拖动时靠它在这棵树里定位节点（id 全局唯一）。
const NODE_PREFIX := "L_"
## 合成区域自己的 UV 矩形（着色器里遮罩/暗角都按这套比例）。
const FULL_UV := Rect2(0.0, 0.0, 1.0, 1.0)


## project_dir：用户工程目录（预览）或构建目录的 assets 目录（导出）。
static func build(doc, project_dir: String) -> Control:
	var root := Control.new()
	root.name = ROOT_NAME
	# 铺满父级：预览时父级是画布 SubViewport，导出后是壁纸窗口 —— 运行时按窗口自适应
	# 正是设计要的行为，所以这里刻意**不**固定成 doc.canvas 的像素尺寸。
	#
	# 必须用 set_anchors_and_offsets_preset，不能只 set_anchors_preset：后者的
	# "keep_offsets=false" 语义是"保留控件当前屏幕位置"，在不在树里的控件上
	# parent_range=0，于是 offset_right/bottom 留成 1920/1080；一进树就成了
	# size = anchor*parent + offset = 1920+1920 = 3840 —— 整张画布放大一倍，
	# 文字和图片全跑出可视区（实测只剩右下角半个色块）。回归用例：
	# test_scene_builder.gd 的 builder_fills_parent_exactly。
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var bg := ColorRect.new()
	bg.name = "background"
	bg.color = doc.background
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(bg)

	# 列表顶部 = 最上层 = 最后 add_child；文档 index 0 是最底层，所以正序累加即可。
	add_children(root, doc.layers, project_dir, FULL_UV, Vector2(doc.canvas), bindings_of(doc))

	# 阶段 2 的接缝：这里会调用 AnimationBuilder.compile(doc, root) 生成 AnimationPlayer。

	# 必须做：PackedScene.pack() 会**静默丢弃** owner != 根节点的子节点
	# （packed_scene.cpp 的 _parse_node 开头就是 `if (p_node != p_owner &&
	#  p_node->get_owner() != p_owner) return OK;`）。不设 owner 的话导出的 .tscn
	# 只有根节点，一个图层都没有 —— 而且 pack() 返回 OK，没有任何报错。
	set_owner_recursive(root, root)
	return root


## 绑定表：目标图层 id -> 绑定在它身上的效果层（按文档顺序，先包的内层）。
## 只收合法的绑定（目标存在、不是效果层、不是自己的祖先），规则集中在
## doc.cover_target_path；这里不重复判断，免得两处规则走偏。
static func bindings_of(doc) -> Dictionary:
	var out := {}
	for item in doc.walk():
		var layer: Dictionary = item["layer"]
		if not Layer.is_effect(layer) or not bool(layer.get("visible", true)):
			continue
		if String(layer.get("cover", {}).get("mode", "all")) != "layer":
			continue
		var target: PackedInt32Array = doc.cover_target_path(item["path"])
		if target.is_empty():
			continue
		var key := String(doc.layer_at(target).get("id", ""))
		if key.is_empty():
			continue
		if not out.has(key):
			out[key] = []
		out[key].append(layer)
	return out


## 把一串兄弟图层建成节点并按顺序挂到 parent 下。
##
## `built` 里一项 = 一个会进场景的兄弟（普通图层或已合成的效果层）。
## 效果层只往前看：它覆盖的是**它下方**已经建好的节点，所以这里是"折叠"而不是递归。
## bindings = 目标图层 id -> 绑定在它身上的效果层（没有绑定就是空字典）。
static func add_children(parent: Node, children: Array, project_dir: String,
		parent_uv: Rect2, parent_size: Vector2, bindings: Dictionary = {}) -> void:
	var built := []
	for raw in children:
		if not (raw is Dictionary):
			continue
		var layer: Dictionary = raw
		if Layer.is_effect(layer):
			# 隐藏的效果层 = 不生效：被它覆盖的图层保持原样，一个节点都不动。
			if not bool(layer.get("visible", true)):
				continue
			# 绑定模式：不在自己位置上生成节点，目标那一层会就地被包起来（见下）。
			if not Layer.cover_target(layer).is_empty() \
					or String(layer.get("cover", {}).get("mode", "")) == "layer":
				continue
			# build_effect 会自己把合成节点插回被覆盖区间原来的位置（保持叠放次序）。
			build_effect(layer, built, project_dir, parent_size)
			continue
		var node: Control = build_layer(layer, project_dir, parent_uv, parent_size, bindings)
		# 绑定在这一层上的效果层：就地把它包起来。目标的叠放位置不变，
		# 所以"上面的图层"仍然是上面的图层。
		for eff in bindings.get(String(layer.get("id", "")), []):
			node = _compose_effect(eff, [{"layer": layer, "node": node}], project_dir,
					parent_size)
		built.append({"layer": layer, "node": node})
	for item in built:
		parent.add_child(item["node"])


## 单个内容图层 -> 节点。parent_uv 是父级（画布或容器）在画布里的比例矩形，
## parent_size 是父级的设计像素尺寸。两者用来把本层的框换算成锚点与 UV。
static func build_layer(layer: Dictionary, project_dir: String, parent_uv: Rect2,
		parent_size: Vector2, bindings: Dictionary = {}) -> Control:
	var box := Control.new()
	box.name = NODE_PREFIX + String(layer.get("id", "layer"))
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	apply_layer_node(box, layer, parent_size)

	var fr := Frame.of(layer, parent_size)
	var content: Control
	if Layer.is_group(layer):
		content = _build_group(layer, project_dir, uv_of(fr, parent_size, parent_uv), fr.size,
				bindings)
	else:
		content = LayerNodes.build_content(layer, project_dir)
		content.name = "content"
	content.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var mat := LayerNodes.blend_material(String(layer.get("blend", "mix")))
	if mat != null:
		if Layer.is_group(layer):
			# 容器自己的材质不能直接挂到子层上（会盖掉子层自己的混合），
			# 所以把整棵子树渲进一个 SubViewport，材质挂在容器上。
			content = _wrap_viewport(content, fr.size, mat)
		else:
			LayerNodes.apply_material(content, mat)

	box.add_child(content)
	return box


static func _build_group(layer: Dictionary, project_dir: String, group_uv: Rect2,
		group_size: Vector2, bindings: Dictionary = {}) -> Control:
	var box := Control.new()
	box.name = "group"
	box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.clip_contents = bool(layer.get("clip", true))
	var kids = layer.get("children")
	if kids is Array:
		add_children(box, kids, project_dir, group_uv, group_size, bindings)
	return box


# ---------------------------------------------------------------- 效果图层

## 效果层：把它覆盖的兄弟节点收进一个合成节点，再套遮罩 / 滤镜。
##
## `built` 会被就地改写：被收走的节点从列表里摘掉，合成节点**插回它们原来所在的
## 位置**（而不是append 到末尾）。位置很重要 —— cover 可以是"第 2~3 层"这种区间，
## 这时紧挨着效果层下面的第 1 层不受影响，必须继续画在合成结果**上面**。
## 返回合成节点；null 表示"下面没有可覆盖的图层"（效果层什么也不做）。
static func build_effect(effect: Dictionary, built: Array, project_dir: String,
		parent_size: Vector2) -> Control:
	var m := built.size()
	var span := Layer.cover_range(effect, m)
	if span.y < span.x:
		return null
	# 位置 p（1 = 紧邻下方）对应 built 下标 m - p。
	var lo: int = m - span.y      # 覆盖区间最下面一层
	var hi: int = m - span.x      # 最上面一层（含）
	var covered := []
	for i in range(hi, lo - 1, -1):
		covered.push_front(built[i])       # 自下而上，和叠放次序一致
	for i in range(hi, lo - 1, -1):
		built.remove_at(i)

	var node := _compose_effect(effect, covered, project_dir, parent_size)
	built.insert(lo, {"layer": effect, "node": node})
	return node


## 被覆盖内容 -> 一个合成节点：染色副本垫底 → 原节点 → 模糊链 → 逐像素材质。
## 区间模式（"第几层"）和绑定模式（"只包这一层"）都走这里，保证两种覆盖方式
## 画出来的东西一模一样。
static func _compose_effect(effect: Dictionary, covered: Array, project_dir: String,
		parent_size: Vector2) -> Control:
	var stack := Control.new()
	stack.name = "covered"
	stack.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	stack.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# 阴影 / 发光 / 描边 = 染色副本垫在下面（滤镜层才可能开这些）。
	for copy in _copies_of(effect, covered, parent_size):
		stack.add_child(copy)
	for item in covered:
		stack.add_child(item["node"])

	var node: Control = stack
	var mat := effect_material(effect, parent_size, project_dir,
			content_uv(covered, parent_size))
	var blur := 0.0
	if Layer.is_filter(effect):
		blur = Filters.blur_radius(effect.get("filters", {}))
	if blur > 0.0:
		# 先模糊再调色（和老的单层滤镜同一顺序），材质挂在最外层。
		node = LayerNodes.wrap_blur(stack, blur, parent_size)
		if mat != null:
			node.material = mat
	elif mat != null:
		node = _wrap_viewport(stack, parent_size, mat)

	node.name = NODE_PREFIX + String(effect.get("id", "effect"))
	node.mouse_filter = Control.MOUSE_FILTER_IGNORE
	node.set_meta("dstk_effect", true)
	return node


## 被覆盖内容的包围盒，换算成父级的 UV 比例（0..1）。
## 暗角这类"按区域算"的滤镜拿它当坐标空间，所以滤镜**绑定到一个小元素**时，
## 暗角是相对那个元素而不是整张画布 —— 否则"只盖一个元素"看起来像没生效。
## 旋转不参与包围盒（按未旋转的框算），和选区框一致。
static func content_uv(covered: Array, parent_size: Vector2) -> Rect2:
	var r := Rect2()
	var first := true
	for item in covered:
		var layer: Dictionary = item["layer"]
		if Layer.is_effect(layer):
			# 效果层没有"自己的内容框"（遮罩框是形状、滤镜层根本没有几何），不参与包围盒。
			continue
		var fr := Frame.of(layer, parent_size)
		r = fr if first else r.merge(fr)
		first = false
	if first:
		return FULL_UV
	var pw := maxf(1.0, parent_size.x)
	var ph := maxf(1.0, parent_size.y)
	return Rect2(Vector2(r.position.x / pw, r.position.y / ph),
			Vector2(r.size.x / pw, r.size.y / ph))


## 滤镜层的"副本"类效果（阴影 / 描边 / 发光）。
## 粒子不复制（复制等于多开一套发射器，数量翻倍）；已经是合成节点的也不复制
## （复制它等于把整棵子树再渲染一遍）。
static func _copies_of(effect: Dictionary, covered: Array, parent_size: Vector2) -> Array:
	var out := []
	if not Layer.is_filter(effect):
		return out
	var f: Dictionary = effect.get("filters", {})
	if not Filters.needs_copy(f):
		return out
	for item in covered:
		var layer: Dictionary = item["layer"]
		var node: Control = item["node"]
		if String(layer.get("type", "")) == "particles":
			continue
		if node.has_meta("dstk_effect"):
			continue
		if Filters.on(f, Filters.SHADOW):
			var g: Dictionary = f[Filters.SHADOW]
			out.append(_copy_tinted(node, g.get("color"), float(g.get("alpha", 0.45)),
					Vector2(float(g.get("x", 0.0)), float(g.get("y", 0.0)))))
		if Filters.on(f, Filters.STROKE):
			var s: Dictionary = f[Filters.STROKE]
			var w := float(s.get("width", 3.0))
			if w > 0.0:
				# 八方向各一份，凑出描边；半径大时会有轻微"重影"，但不需要额外渲染趟。
				for dir in [Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1),
						Vector2(0.707, 0.707), Vector2(-0.707, 0.707),
						Vector2(0.707, -0.707), Vector2(-0.707, -0.707)]:
					out.append(_copy_tinted(node, s.get("color"), 1.0, dir * w))
		if Filters.on(f, Filters.GLOW):
			var g2: Dictionary = f[Filters.GLOW]
			var radius := float(g2.get("radius", 20.0))
			var copy := _copy_tinted(node, g2.get("color"),
					float(g2.get("alpha", 0.55)), Vector2.ZERO)
			out.append(LayerNodes.wrap_blur(copy, radius, parent_size) if radius > 0.0 else copy)
	return out


static func _copy_tinted(content: Control, color, alpha: float, offset: Vector2) -> Control:
	var dup: Control = content.duplicate()
	var c := Layer.to_color(color, Color.BLACK)
	dup.modulate = Color(c.r, c.g, c.b, clampf(alpha, 0.0, 1.0))
	# 内容节点是锚点节点：位移只能写 offsets，写 position 会被布局覆盖。
	dup.offset_left += offset.x
	dup.offset_top += offset.y
	dup.offset_right += offset.x
	dup.offset_bottom += offset.y
	dup.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return dup


## 效果层需要的材质：遮罩层 -> 遮罩着色器；滤镜层 -> 逐像素调色（色彩/暗角/色调分离）。
## region 是被覆盖内容的包围盒（父级 UV），只有暗角用得到。
## 返回 null 表示这一层不需要着色器（比如只开了模糊，或者什么都没勾）。
static func effect_material(effect: Dictionary, parent_size: Vector2,
		project_dir: String, region: Rect2 = FULL_UV) -> Material:
	if Layer.is_mask(effect):
		return _material(_mask_uniforms(effect, parent_size, project_dir, true))
	var f: Dictionary = effect.get("filters", {})
	if not Filters.needs_material(f):
		return null
	return _material(_grade_uniforms(f, region))


static func _grade_uniforms(f: Dictionary, region: Rect2 = FULL_UV) -> Dictionary:
	return Filters.grade_uniforms(f, region)


## 遮罩层的 uniform。遮罩矩形按**合成区域自己的 UV 比例**给（0..1），
## 所以容器嵌套、窗口缩放都不会错位。
static func _mask_uniforms(mask_layer: Dictionary, parent_size: Vector2,
		project_dir: String, with_texture: bool) -> Dictionary:
	# 恒等色彩：同一个着色器兼遮罩与调色，遮罩层只要遮罩那一段。
	var uniforms := _grade_uniforms({}, FULL_UV)
	var code := Mask.shape_code(mask_layer)
	var fr := Frame.of(mask_layer, parent_size)
	var r := Mask.uv_rect(mask_layer, parent_size)
	var radius := float(mask_layer.get("radius", 0.0))
	var feather := float(mask_layer.get("feather", 0.0))
	uniforms["mask_shape"] = code
	# 几何一律压成 Vector4：set_shader_parameter 收到 Rect2 会直接把进程打挂。
	uniforms["mask_rect"] = Vector4(r.position.x, r.position.y, r.size.x, r.size.y)
	uniforms["mask_radii"] = Vector2(
			clampf(radius / maxf(1.0, fr.size.x), 0.0, 0.5),
			clampf(radius / maxf(1.0, fr.size.y), 0.0, 0.5))
	uniforms["mask_feather"] = Vector2(
			feather / maxf(1.0, fr.size.x), feather / maxf(1.0, fr.size.y))
	uniforms["mask_invert"] = bool(mask_layer.get("invert", false))
	if code == 3 and with_texture:
		var tex := Mask.load_texture(mask_layer, project_dir)
		if tex == null:
			# 遮罩图丢了就整块遮住：明显可见的错误，比"当没设遮罩"更难漏掉。
			var blank := Image.create(1, 1, false, Image.FORMAT_RGBA8)
			blank.fill(Color(0, 0, 0, 0))
			tex = ImageTexture.create_from_image(blank)
		uniforms["mask_tex"] = tex
	return uniforms


## 按混合模式生成图层着色器（引擎按源码哈希复用已编译结果，所以每层新建对象没问题）。
static func _material(uniforms: Dictionary) -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = Filters.layer_shader("mix")
	var mat := ShaderMaterial.new()
	mat.shader = shader
	for key in uniforms.keys():
		mat.set_shader_parameter(key, uniforms[key])
	# 打个标记：拖动遮罩时只更新这类材质的 uniform，不重建节点（见 refresh_effect）。
	mat.set_meta("dstk_effect_style", true)
	return mat


## 拖动遮罩层时**不重建节点**：只把新的 uniform 写进已有的材质。
## path 指向效果层；找不到节点/材质时静默返回（比如它下面没层、被隐藏了、绑定失效）。
static func refresh_effect(root: Node, doc, path: PackedInt32Array,
		project_dir: String) -> void:
	if root == null or path.is_empty():
		return
	var layer: Dictionary = doc.layer_at(path)
	if layer.is_empty() or not Layer.is_effect(layer):
		return
	var node := root.find_child(NODE_PREFIX + String(layer.get("id", "")), true, false)
	if node == null or not node.has_meta("dstk_effect"):
		return
	if not (node is CanvasItem):
		return
	var mat: Material = (node as CanvasItem).material
	if not (mat is ShaderMaterial) or not mat.has_meta("dstk_effect_style"):
		return
	# 坐标空间是"被覆盖内容所在的父级"：绑定模式下目标可能在别的容器里。
	var space: PackedInt32Array = doc.effect_space_path(path)
	if space.is_empty():
		return
	var parent_size: Vector2 = doc.parent_size(space)
	var uniforms := {}
	if Layer.is_mask(layer):
		uniforms = _mask_uniforms(layer, parent_size, project_dir, false)
	else:
		uniforms = _grade_uniforms(layer.get("filters", {}),
				content_uv(covered_items_of(doc, path), parent_size))
	for key in uniforms.keys():
		(mat as ShaderMaterial).set_shader_parameter(key, uniforms[key])


## 文档侧的"被覆盖图层"（拖动时算包围盒用；不建节点）。
## 绑定模式 = 目标那一层，其余模式 = 同父级里 span 覆盖的那几层。
static func covered_items_of(doc, path: PackedInt32Array) -> Array:
	var out := []
	if path.is_empty():
		return out
	var target: PackedInt32Array = doc.cover_target_path(path)
	if not target.is_empty():
		out.append({"layer": doc.layer_at(target)})
		return out
	var span: Vector2i = doc.cover_span(path)
	var arr: Array = doc.siblings_at(doc.parent_path_of(path))
	for i in range(span.x, span.y):
		if i >= 0 and i < arr.size() and (arr[i] is Dictionary):
			out.append({"layer": arr[i]})
	return out


## 把一段内容渲进 SubViewport，材质挂在最外层的容器上（材质只影响这一个节点，
## 所以想"整棵子树一起变"就必须先合成成一张图）。
static func _wrap_viewport(content: Control, size: Vector2, material: Material) -> Control:
	var vp := SubViewport.new()
	vp.name = "viewport"
	vp.size = Vector2i(maxi(1, int(size.x)), maxi(1, int(size.y)))
	vp.transparent_bg = true
	vp.disable_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	vp.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS

	var cont := SubViewportContainer.new()
	cont.name = "viewport_container"
	cont.stretch = true
	cont.mouse_filter = Control.MOUSE_FILTER_IGNORE
	cont.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	cont.add_child(vp)
	cont.material = material

	content.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	content.offset_left = 0.0
	content.offset_top = 0.0
	content.offset_right = 0.0
	content.offset_bottom = 0.0
	vp.add_child(content)
	return cont


## 把整棵子树的 owner 指到 scene_root。预览时不需要，导出时是必需的。
static func set_owner_recursive(node: Node, scene_root: Node) -> void:
	for c in node.get_children():
		c.owner = scene_root
		set_owner_recursive(c, scene_root)


## 框在画布里的比例矩形。父级的 UV 矩形 + 本层在父级里的相对位置。
static func uv_of(fr: Rect2, parent_size: Vector2, parent_uv: Rect2) -> Rect2:
	var pw := maxf(1.0, parent_size.x)
	var ph := maxf(1.0, parent_size.y)
	return Rect2(
		parent_uv.position + Vector2(fr.position.x / pw, fr.position.y / ph) * parent_uv.size,
		Vector2(fr.size.x / pw, fr.size.y / ph) * parent_uv.size)


## 位置/尺寸/旋转/不透明度/可见性。显式框用**锚点**表达（像素 / 父级尺寸），
## 于是导出的场景在任意窗口尺寸下都等比伸缩；没有显式框的图层保持"铺满父级"的老语义。
##
## 效果图层没有自己的几何：遮罩框只是着色器 uniform，合成节点永远铺满父级，
## 所以这里对它们直接返回（拖动遮罩时由 refresh_effect 更新 uniform）。
static func apply_layer_node(node: Control, layer: Dictionary, parent_size: Vector2) -> void:
	if Layer.is_effect(layer):
		node.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		node.visible = bool(layer.get("visible", true))
		return
	var t: Dictionary = layer.get("transform", {})
	var explicit := float(t.get("w", 0.0)) > 0.0 and float(t.get("h", 0.0)) > 0.0
	if explicit:
		var a := Frame.anchors(layer, parent_size)
		node.anchor_left = float(a["left"])
		node.anchor_top = float(a["top"])
		node.anchor_right = float(a["right"])
		node.anchor_bottom = float(a["bottom"])
		# 锚点写完之后偏移必须归零，否则位置 = 锚点*父级 + 偏移，框会整体漂移。
		node.offset_left = 0.0
		node.offset_top = 0.0
		node.offset_right = 0.0
		node.offset_bottom = 0.0
	else:
		node.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		node.position = Frame.of(layer, parent_size).position
	node.rotation_degrees = float(t.get("rotation", 0.0))
	node.modulate = Color(1, 1, 1, float(t.get("opacity", 1.0)))
	node.visible = bool(layer.get("visible", true))
	node.mouse_filter = Control.MOUSE_FILTER_IGNORE
