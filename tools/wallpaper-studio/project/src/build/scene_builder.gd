extends RefCounted
## 文档 -> 完整 Node 树。**预览和导出共用这一条路径**（设计 §2.3 的不变式）。
##
## 图层是树（容器可以嵌套），所以这里也是递归的：每个图层一个 box，
## box 的锚点 = 它相对父级的框；容器里的子层以容器的框为参照。
## 于是"移动/缩放容器 -> 子元素跟着走"在运行时同样成立（锚点是比例，不是像素）。

const LayerNodes = preload("res://src/build/layer_nodes.gd")
const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")

const ROOT_NAME := "WallpaperRoot"
## 图层节点名的前缀 + 图层 id。拖动时靠它在这棵树里定位节点（id 全局唯一）。
const NODE_PREFIX := "L_"


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
	var canvas := Vector2(doc.canvas)
	var root_uv := Rect2(Vector2.ZERO, Vector2.ONE)
	for layer in doc.layers:
		root.add_child(build_layer(layer, project_dir, root_uv, canvas))

	# 阶段 2 的接缝：这里会调用 AnimationBuilder.compile(doc, root) 生成 AnimationPlayer。

	# 必须做：PackedScene.pack() 会**静默丢弃** owner != 根节点的子节点
	# （packed_scene.cpp 的 _parse_node 开头就是 `if (p_node != p_owner &&
	#  p_node->get_owner() != p_owner) return OK;`）。不设 owner 的话导出的 .tscn
	# 只有根节点，一个图层都没有 —— 而且 pack() 返回 OK，没有任何报错。
	set_owner_recursive(root, root)
	return root


## 单个图层 -> 节点。parent_uv 是父级（画布或容器）在画布里的比例矩形，
## parent_size 是父级的设计像素尺寸。两者用来把本层的框换算成锚点与 UV。
static func build_layer(layer: Dictionary, project_dir: String, parent_uv: Rect2,
		parent_size: Vector2) -> Control:
	var box := Control.new()
	box.name = NODE_PREFIX + String(layer.get("id", "layer"))
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	apply_layer_node(box, layer, parent_size)

	var fr := Frame.of(layer, parent_size)
	var uv := uv_of(fr, parent_size, parent_uv)

	var content: Control
	if Layer.is_group(layer):
		content = _build_group(layer, project_dir, uv, fr.size)
	else:
		content = LayerNodes.build_content(layer, project_dir)
		content.name = "content"
	content.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var stack := Control.new()
	stack.name = "stack"
	stack.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	stack.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for copy in LayerNodes.effect_copies(layer, content, fr.size):
		stack.add_child(copy)
	stack.add_child(content)

	var material := LayerNodes.style_material(layer, uv, fr.size, project_dir)
	var blur := Filters.blur_radius(layer.get("filters", {}))
	if blur > 0.0:
		# 模糊先渲染再调色/遮罩：材质挂在最外层容器上，那一层已经回到画布坐标，
		# 屏幕 UV 直接就是画布比例（着色器里 screen_origin/size 用默认值即可）。
		stack = LayerNodes.wrap_blur(stack, blur, fr.size)
		if material != null:
			stack.material = material
	elif material != null:
		if Layer.is_group(layer):
			# 容器自己的材质不能直接挂到子层上（会盖掉子层自己的遮罩/滤镜），
			# 所以把整棵子树渲进一个 SubViewport，材质挂在容器上。
			stack = _wrap_viewport(stack, fr.size, material)
		else:
			LayerNodes.apply_material(content, material)

	box.add_child(stack)
	return box


static func _build_group(layer: Dictionary, project_dir: String, group_uv: Rect2,
		group_size: Vector2) -> Control:
	var box := Control.new()
	box.name = "group"
	box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.clip_contents = bool(layer.get("clip", true))
	var kids = layer.get("children")
	if kids is Array:
		for child in kids:
			if child is Dictionary:
				box.add_child(build_layer(child, project_dir, group_uv, group_size))
	return box


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


## 拖动中的样式刷新：遮罩/滤镜的参数变了，但**节点树没变** —— 只把新的 uniform
## 写进已有的材质，不重建节点、不重新解码遮罩图。path 用来逐级累加出画布 UV。
static func refresh_style(root: Node, doc, path: PackedInt32Array, project_dir: String) -> void:
	if root == null or path.is_empty():
		return
	var parent_uv := Rect2(Vector2.ZERO, Vector2.ONE)
	var parent_size := Vector2(doc.canvas)
	for i in path.size() - 1:
		var anc: Dictionary = doc.layer_at(path.slice(0, i + 1))
		if anc.is_empty():
			return
		var anc_frame := Frame.of(anc, parent_size)
		parent_uv = uv_of(anc_frame, parent_size, parent_uv)
		parent_size = anc_frame.size
	var layer: Dictionary = doc.layer_at(path)
	if layer.is_empty():
		return
	var fr := Frame.of(layer, parent_size)
	var uv := uv_of(fr, parent_size, parent_uv)
	var box := root.find_child(NODE_PREFIX + String(layer.get("id", "")), true, false)
	LayerNodes.update_style(box, layer, uv, fr.size, project_dir)


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
static func apply_layer_node(node: Control, layer: Dictionary, parent_size: Vector2) -> void:
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
