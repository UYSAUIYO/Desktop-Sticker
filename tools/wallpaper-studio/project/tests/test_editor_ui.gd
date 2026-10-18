extends RefCounted
## 编辑器整体装配的冒烟用例：真的把 main.tscn 挂进树，选中图层、走一遍
## "改框 -> apply_live -> 重建"的路径。
##
## 不合成鼠标事件（拖动的手感由人验），但能抓住这类装配错：信号没接上、
## 节点名对不上、apply_live 改不到真实节点、选区框尺寸算成 0。
##
## 注意：无头模式下控件要等一帧才有布局，所以这里直接给画布容器一个尺寸，
## 再手动调 _fit()，不依赖窗口。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")


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
		var sel = editor._canvas._selection
		t.eq(sel.visible, true, "选中图层后选区框可见")
		t.ok(sel.size.x > 100.0 and sel.size.y > 100.0, "选区框跟着画布缩放")

		# 拖右下角：改的是文档里的宽高，节点必须跟着变（apply_live 不重建树）。
		var layer: Dictionary = editor.doc.layers[1]
		var before := Frame.of(layer, editor.doc.canvas)
		Frame.resize(layer, editor.doc.canvas, Frame.RIGHT | Frame.BOTTOM, Vector2(120, 60), false)
		editor.apply_live()
		var node: Control = editor._canvas._root.get_node("layer_1")
		var after := Frame.of(layer, editor.doc.canvas)
		t.approx(after.size.x, before.size.x + 120.0, 0.001, "宽 +120")
		t.approx(node.anchor_right, (after.position.x + after.size.x) / float(editor.doc.canvas.x),
				0.0001, "节点锚点跟着新框走")

		# 画布上点中哪层选哪层；点到外面取消选择。
		editor.apply_change(true)
		var inside: Vector2 = editor._canvas._container.position \
				+ Vector2(10, 10) * editor._canvas._scale
		t.eq(editor._canvas._layer_at(inside), 1, "点画布选中上层")
		t.eq(editor._canvas._layer_at(Vector2(-4000, -4000)), -1, "点空处取消选择")

		tree.root.remove_child(editor)
		editor.free()
	)
