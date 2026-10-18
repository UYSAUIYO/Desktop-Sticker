extends RefCounted
## 文档 -> 完整 Node 树。**预览和导出共用这一条路径**（设计 §2.3 的不变式）。
## 阶段 1 只做 color/image/text/particles 的静态形态；时间轴与效果栈分别在阶段 2/3
## 接入，两个接缝位置在 build() 里以注释标出。

const LayerNodes = preload("res://src/build/layer_nodes.gd")
const Frame = preload("res://src/doc/frame.gd")

const ROOT_NAME := "WallpaperRoot"


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
	for i in doc.layers.size():
		var layer: Dictionary = doc.layers[i]
		var node = LayerNodes.build(layer, project_dir)
		if node == null:
			continue
		node.name = "layer_%d" % i
		apply_common(node, layer, doc.canvas)
		root.add_child(node)

	# 阶段 2 的接缝：这里会调用 AnimationBuilder.compile(doc, root) 生成 AnimationPlayer。
	# 阶段 3 的接缝：LayerNodes.build 会在 layer.effects 非空时返回 SubViewport 链
	#               （链的第 0 趟吃原始节点），而不是原始节点本身。

	# 必须做：PackedScene.pack() 会**静默丢弃** owner != 根节点的子节点
	# （packed_scene.cpp 的 _parse_node 开头就是 `if (p_node != p_owner &&
	#  p_node->get_owner() != p_owner) return OK;`）。不设 owner 的话导出的 .tscn
	# 只有根节点，一个图层都没有 —— 而且 pack() 返回 OK，没有任何报错。
	set_owner_recursive(root, root)
	return root


## 把整棵子树的 owner 指到 scene_root。预览时不需要，导出时是必需的。
static func set_owner_recursive(node: Node, scene_root: Node) -> void:
	for c in node.get_children():
		c.owner = scene_root
		set_owner_recursive(c, scene_root)


## 把通用字段（位置/尺寸 / 变换 / 可见性 / 混合模式）应用到节点上。
static func apply_common(node: Control, layer: Dictionary, canvas: Vector2i) -> void:
	var t: Dictionary = layer.get("transform", {})
	apply_frame(node, layer, canvas)
	node.scale = Vector2(float(t.get("scale", 1.0)), float(t.get("scale", 1.0)))
	node.rotation_degrees = float(t.get("rotation", 0.0))
	node.modulate = Color(1, 1, 1, float(t.get("opacity", 1.0)))
	node.visible = bool(layer.get("visible", true))
	node.mouse_filter = Control.MOUSE_FILTER_IGNORE
	apply_blend(node, String(layer.get("blend", "mix")))


## 位置与尺寸。显式框（transform.w/h > 0）用**锚点**表达：锚点值 = 画布像素 / 画布，
## 于是导出的场景在任意窗口尺寸下都等比伸缩，和"铺满画布"的老图层行为一致。
## 没有显式框的图层保持"铺满父级"的老语义（整张画布）。
static func apply_frame(node: Control, layer: Dictionary, canvas: Vector2i) -> void:
	var t: Dictionary = layer.get("transform", {})
	var explicit := float(t.get("w", 0.0)) > 0.0 and float(t.get("h", 0.0)) > 0.0
	if explicit:
		var a := Frame.anchors(layer, canvas)
		node.anchor_left = float(a["left"])
		node.anchor_top = float(a["top"])
		node.anchor_right = float(a["right"])
		node.anchor_bottom = float(a["bottom"])
		# 锚点写完之后偏移必须归零，否则位置 = 锚点*父级 + 偏移，框会整体漂移。
		node.offset_left = 0.0
		node.offset_top = 0.0
		node.offset_right = 0.0
		node.offset_bottom = 0.0
		return
	node.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	node.position = Frame.of(layer, canvas).position


static func apply_blend(node: Control, blend: String) -> void:
	if blend == "mix":
		return
	var mat := CanvasItemMaterial.new()
	match blend:
		"add":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
		"sub":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_SUB
		"mul":
			mat.blend_mode = CanvasItemMaterial.BLEND_MODE_MUL
		_:
			# screen / overlay 阶段 3 用自定义着色器实现，现在先当 mix 处理。
			return
	node.material = mat
