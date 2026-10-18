extends RefCounted
## 编辑器整体装配的冒烟用例：真的把 main.tscn 挂进树，选中图层、走一遍
## "改框 -> apply_live -> 重建 -> 点选"的路径。
##
## 不合成鼠标事件（拖动的手感由人验），但能抓住这类装配错：信号没接上、
## 节点名对不上、apply_live 改不到真实节点、容器嵌套后点选进不去子层。
##
## 注意：无头模式下控件要等一帧才有布局，所以这里直接给画布容器一个尺寸，
## 再手动调 _fit()，不依赖窗口。

const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")


func _node_of(editor, layer: Dictionary) -> Control:
	return editor._canvas._root.find_child("L_" + String(layer.get("id", "")), true, false)


func register(t) -> void:
	t.case("editor_assembly_selects_and_drags", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		editor.doc.add_layer(Layer.make_image("图", ""))
		editor.apply_change(true)

		# 布局：无头下控件还没排过版，直接给尺寸并触发一次适配。
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()

		editor.select_layer(1)
		t.eq(editor._canvas._selection.visible, true, "选中图层后选区层可见")
		t.eq(editor.sel, PackedInt32Array([1]), "选中路径 = [1]")

		# 拖右下角：改的是文档里的宽高，节点必须跟着变（apply_live 不重建树）。
		var layer: Dictionary = editor.doc.layers[1]
		var before := Frame.of(layer, editor.doc.canvas)
		Frame.resize(layer, editor.doc.canvas, Frame.RIGHT | Frame.BOTTOM, Vector2(120, 60), false)
		editor.apply_live()
		var node := _node_of(editor, layer)
		var after := Frame.of(layer, editor.doc.canvas)
		t.approx(after.size.x, before.size.x + 120.0, 0.001, "宽 +120")
		t.approx(node.anchor_right, (after.position.x + after.size.x) / float(editor.doc.canvas.x),
				0.0001, "节点锚点跟着新框走")

		# 画布上点中哪层选哪层；点到外面取消选择。
		editor.apply_change(true)
		var inside: Vector2 = editor._canvas._container.position \
				+ Vector2(10, 10) * editor._canvas._scale
		t.eq(editor._canvas.layer_at_screen(inside), PackedInt32Array([1]), "点画布选中上层")
		t.eq(editor._canvas.layer_at_screen(Vector2(-4000, -4000)).size(), 0, "点空处取消选择")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_group_nesting_selects_child", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		editor.doc.add_layer(Layer.make_text("标题", "你好"))
		editor.doc.layers = [editor.doc.layers[0],
				editor.doc.layers[1]]
		editor.apply_change(true)

		# 把标题包进容器：容器路径 [1]，子层 [1,0]。
		var group_path: PackedInt32Array = editor.doc.wrap_in_group(PackedInt32Array([1]))
		t.eq(group_path, PackedInt32Array([1]), "容器占据原图层的位置")
		editor.apply_change(true)

		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()

		editor.select_layer_path(PackedInt32Array([1, 0]))
		t.eq(String(editor.selected_layer().get("name", "")), "标题", "选中的是容器里的子层")

		# 容器框 = 原子层的框（整张画布），子层铺满容器 -> 画布中心命中的是子层。
		var center: Vector2 = editor._canvas._container.position \
				+ Vector2(editor.doc.canvas) * 0.5 * editor._canvas._scale
		t.eq(editor._canvas.layer_at_screen(center), PackedInt32Array([1, 0]),
				"点画布命中容器里的子层")
		t.eq(editor._canvas.outer_group_at_screen(center), PackedInt32Array([1]),
				"Alt 选外层容器")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_inspector_builds_mask_and_filter_sections", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		var l := Layer.make_text("标题", "你好")
		l["mask"] = Mask.normalize({"enabled": true, "shape": "rounded", "w": 100.0, "h": 50.0})
		var f := Filters.normalize({})
		f[Filters.COLOR]["enabled"] = true
		f[Filters.BLUR]["enabled"] = true
		l["filters"] = f
		editor.doc.add_layer(l)
		editor.apply_change(true)
		editor.select_layer(0)

		# 属性面板建出来（含遮罩/滤镜控件）不应报错，且内容节点带着材质。
		var before: int = editor._inspector._box.get_child_count()
		editor._inspector.refresh(true)
		t.ok(editor._inspector._box.get_child_count() == before, "属性面板段落数稳定")
		t.ok(before > 20, "遮罩/滤镜控件确实建了一堆")
		t.ok(_node_of(editor, l).find_child("*", true, false) != null, "节点树建出来了")

		tree.root.remove_child(editor)
		editor.free()
	)
