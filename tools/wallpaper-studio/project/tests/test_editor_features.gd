extends RefCounted
## 编辑器"手感类"能力的用例：撤销/重做、画布缩放平移、多选与框选、复制粘贴、
## 图层缩略图与折叠，以及**贝塞尔运动路径**的纯逻辑。
##
## 这些功能一半在 UI 层，但它们的状态（历史栈、选择集合、缩放、路径点）都是
## 可断言的数据，所以仍然能在无头进程里钉住 —— 真正只有"看到的画面"能验证的
## 部分交给 tests/ui_walkthrough.gd（带窗口跑）。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")
const Timeline = preload("res://src/doc/timeline.gd")
const Motion = preload("res://src/doc/motion.gd")
const LayerThumbs = preload("res://src/ui/layer_thumbs.gd")


func register(t) -> void:
	_register_undo(t)
	_register_view(t)
	_register_selection(t)
	_register_motion(t)


# ---------------------------------------------------------------- 撤销 / 重做

func _register_undo(t) -> void:
	t.case("editor_undo_redo_restores_document", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		var layer := Layer.make_color("底", Color(0.1, 0.2, 0.3))
		editor.doc.add_layer(layer)
		editor.apply_change(true)

		t.eq(editor.can_undo(), true, "改过之后可以撤销")
		t.eq(editor.can_redo(), false, "还没有可重做的")

		# 两步之间断开合并：连续同类操作（同一种 label、700ms 内）本来就该合成一步，
		# 这里要的是两步独立的历史。
		editor.break_history()
		editor.doc.layers[0]["color"] = Color(0.9, 0.1, 0.1)
		editor.apply_change(false)
		editor.undo()
		t.eq(editor.doc.layers.size(), 1, "撤销后图层还在（回到加层之后）")
		# 颜色经过 #rrggbb 写盘，按 8bit 量化后比较。
		t.eq(String(editor.doc.layers[0].get("color", Color()).to_html(false)),
				Color(0.1, 0.2, 0.3).to_html(false), "撤销回到改色之前")
		t.eq(editor.can_redo(), true, "撤销之后可以重做")

		editor.redo()
		t.eq(String(editor.doc.layers[0].get("color", Color()).to_html(false)),
				Color(0.9, 0.1, 0.1).to_html(false), "重做把改动拿回来")

		# 一路退到底：回到刚打开的空文档。
		editor.undo()
		editor.undo()
		t.eq(editor.doc.layers.size(), 0, "退到底 = 空工程")
		t.eq(editor.can_undo(), false, "到底之后没有可撤销的了")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_text_roundtrip_keeps_colors", func() -> void:
		# 回归：JSON.stringify 会把 Color 落成 "(0.1, 0.2, 0.3, 1.0)"，读回来认不出，
		# 颜色会静默变成默认色 —— 表现是"存一次盘 / 撤一次销，颜色全变了"。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("块", Color(0.1, 0.2, 0.3)))
		var group := Layer.make_group("组", 400.0, 300.0)
		doc.add_layer(group)
		doc.insert_layer(PackedInt32Array([1]), Layer.make_text("字", "hi"))
		var text: String = doc.to_text()
		t.ok(text.find("\"#") > 0, "颜色写成 #rrggbb（不是 Color 的 str）")
		var parsed: Dictionary = StudioDoc.parse(text)
		t.eq(parsed.ok, true, "能读回来：" + String(parsed.error))
		if parsed.ok:
			var back: Dictionary = parsed.doc.layers[0]
			t.eq(String((back.get("color", Color()) as Color).to_html(false)),
					Color(0.1, 0.2, 0.3).to_html(false), "纯色层的颜色原样回来")
			var kids: Array = parsed.doc.layers[1].get("children", [])
			if kids.size() == 1:
				t.eq(String((kids[0].get("color", Color()) as Color).to_html(false)),
						Color(0.9, 0.95, 1.0).to_html(false), "容器里子层的颜色也回来了")
	)

	t.case("editor_undo_merges_continuous_edits", func() -> void:
		# 拖滑块 / 连拖关键帧会刷出几十次 apply_change，逐次记一步的话
		# "撤一次"只退一点点。同标签的连续提交必须合成一步。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		editor.apply_change(true)

		editor.doc.layers[0]["transform"]["x"] = 10.0
		editor.apply_change(false)
		editor.doc.layers[0]["transform"]["x"] = 20.0
		editor.apply_change(false)
		editor.doc.layers[0]["transform"]["x"] = 30.0
		editor.apply_change(false)

		editor.undo()
		t.eq(editor.doc.layers.size(), 0, "连续三次改动合成一步：撤一次就回到加层之前")
		editor.redo()
		t.approx(float(editor.doc.layers[0]["transform"]["x"]), 30.0, 0.001,
				"重做回到最后一次的值")

		# break_history() 之后是新的一段：不跟前面合并。
		editor.break_history()
		editor.doc.layers[0]["transform"]["x"] = 99.0
		editor.apply_change(false)
		editor.undo()
		t.approx(float(editor.doc.layers[0]["transform"]["x"]), 30.0, 0.001,
				"断开合并后，撤一次只退一步")

		tree.root.remove_child(editor)
		editor.free()
	)


# ---------------------------------------------------------------- 画布视图

func _register_view(t) -> void:
	t.case("editor_canvas_zoom_keeps_point_under_cursor", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)
		editor.doc.canvas = Vector2i(1280, 720)
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()

		var fit: float = editor._canvas.zoom_level()
		t.approx(fit, minf((800.0 - 56.0) / 1280.0, (600.0 - 56.0) / 720.0), 0.01,
				"起步是「适应窗口」")

		var anchor := Vector2(300.0, 200.0)
		var before: Vector2 = editor._canvas.to_canvas(anchor)
		editor._canvas.set_zoom(2.0, anchor)
		t.approx(editor._canvas.zoom_level(), fit * 2.0, 0.01, "放大到 200%（相对适应）")
		var after: Vector2 = editor._canvas.to_canvas(anchor)
		t.ok(before.distance_to(after) < 1.0,
				"缩放锚在光标上：同一个屏幕点还是同一个画布点（差 %.2f）"
						% before.distance_to(after))

		# 平移 40px：画布跟着走，所以同一个屏幕点对应的画布坐标反向挪 40/缩放。
		editor._canvas.pan_by(Vector2(40.0, -25.0))
		var panned: Vector2 = editor._canvas.to_canvas(anchor)
		t.approx(after.x - panned.x, 40.0 / editor._canvas.zoom_level(), 0.5,
				"右移 40px = 画布坐标左移 40/缩放")
		t.approx(panned.y - after.y, 25.0 / editor._canvas.zoom_level(), 0.5,
				"上移 25px = 画布坐标下移 25/缩放")

		editor._canvas.fit_view()
		t.approx(editor._canvas.zoom_level(), fit, 0.01, "「适应」回到刚好放得下")

		tree.root.remove_child(editor)
		editor.free()
	)


# ---------------------------------------------------------------- 多选 / 框选 / 剪贴板

func _register_selection(t) -> void:
	t.case("editor_multi_selection_and_rubber_band", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)
		editor.doc.canvas = Vector2i(1280, 720)
		for i in 3:
			var l := Layer.make_color("块%d" % i, Color.WHITE)
			l["transform"]["x"] = 100.0 * float(i)
			l["transform"]["y"] = 0.0
			l["transform"]["w"] = 80.0
			l["transform"]["h"] = 80.0
			editor.doc.add_layer(l)
		editor.apply_change(true)
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()

		editor.set_selection([PackedInt32Array([0]), PackedInt32Array([1])])
		t.eq(editor.selected_paths().size(), 2, "选中两个图层")
		t.eq(editor.sel, PackedInt32Array([1]), "最后点到的那个是主选择")
		t.eq(editor.has_multi_selection(), true, "处于多选状态")

		editor.toggle_selection(PackedInt32Array([1]))
		t.eq(editor.selected_paths().size(), 1, "再点一次 = 反选")

		# 框选：用一个覆盖左半边的矩形，只框住前两个（第三个在 200 之外）。
		var rect := Rect2(Vector2.ZERO, Vector2(editor._canvas._container.position.x + 190.0
				* editor._canvas._scale, 600.0))
		var hits: Array = editor._canvas._selection._layers_in_rect(rect)
		t.eq(hits.size(), 2, "框选命中两个图层")
		t.eq(hits.has(PackedInt32Array([2])), false, "框外的没被选中")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_copy_paste_makes_independent_layers", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)
		editor.doc.canvas = Vector2i(1280, 720)

		# 容器 + 子层：复制必须带上整棵子树，而且 id 全换新的。
		var group := Layer.make_group("组", 400.0, 300.0)
		editor.doc.add_layer(group)
		editor.apply_change(true)
		var child := Layer.make_color("子", Color(0.2, 0.4, 0.6))
		editor.doc.insert_layer(PackedInt32Array([0]), child)
		editor.apply_change(true)

		# 选中子层复制 + 粘贴：副本落在同一个容器里（粘进主选择的父级）。
		editor.select_layer_path(PackedInt32Array([0, 0]))
		editor.copy_selection()
		editor.paste_layers()
		t.eq(editor.doc.layers.size(), 1, "粘贴没有多出一个根层")
		var kids0: Array = editor.doc.layers[0].get("children", [])
		t.eq(kids0.size(), 2, "容器里多了一份副本")
		if kids0.size() == 2:
			var copy: Dictionary = kids0[1]
			t.eq(String(copy.get("name", "")), "子 副本", "副本名字带了后缀")
			t.ok(String(copy.get("id", "")) != String(child.get("id", "")),
					"副本换了 id（否则节点名会撞）")
			# 改副本不该动到原件。
			copy["color"] = Color(1, 0, 0)
			t.eq(kids0[0].get("color", Color()), Color(0.2, 0.4, 0.6), "副本是独立的一份")

		# 原地复制容器：必须是并列的一份，不能把容器复制进它自己里面。
		editor.select_layer_path(PackedInt32Array([0]))
		editor.duplicate_selection()
		t.eq(editor.doc.layers.size(), 2, "原地复制出并列的一份")
		if editor.doc.layers.size() == 2:
			var dup: Dictionary = editor.doc.layers[1]
			t.eq(String(dup.get("name", "")), "组 副本", "原地复制也带后缀")
			t.approx(float((dup.get("transform", {}) as Dictionary).get("x", 0.0)), 24.0,
					0.001, "副本错开了一点")
			var dupkids: Array = dup.get("children", [])
			t.eq(dupkids.size(), 2, "整棵子树跟着复制")
			if dupkids.size() == 2:
				t.ok(String(dupkids[0].get("id", "")) != String(kids0[0].get("id", "")),
						"子层也换了 id")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_delete_removes_whole_selection", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)
		editor.doc.canvas = Vector2i(1280, 720)
		for i in 3:
			editor.doc.add_layer(Layer.make_color("块%d" % i, Color.WHITE))
		editor.apply_change(true)
		editor.set_selection([PackedInt32Array([0]), PackedInt32Array([1])])
		editor.delete_selection()
		t.eq(editor.doc.layers.size(), 1, "多选删除掉了两个")
		t.eq(String(editor.doc.layers[0].get("name", "")), "块2",
				"删的是选中的那两个（下标没串位）")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_layer_thumbs_and_folding", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)
		editor.doc.canvas = Vector2i(1280, 720)

		var thumbs = LayerThumbs.new()
		var icon := thumbs.icon_for(Layer.make_color("底", Color(0.3, 0.5, 0.7)), "")
		t.ok(icon != null, "纯色层有缩略图")
		if icon != null:
			t.eq((icon as Texture2D).get_size(), Vector2i(24, 24), "图标 24×24")
		t.ok(thumbs.icon_for(Layer.make_text("标题", "hi"), "") != null, "文字层有图标")
		t.ok(thumbs.icon_for(Layer.make_particles("粒子", ""), "") != null, "粒子层有图标")
		t.ok(thumbs.icon_for(Layer.make_mask("遮罩"), "") != null, "遮罩层有图标")

		# 折叠：容器收起来之后子层不再出现在列表里。
		var group := Layer.make_group("组", 400.0, 300.0)
		editor.doc.add_layer(group)
		editor.doc.insert_layer(PackedInt32Array([0]), Layer.make_color("子", Color.WHITE))
		editor.apply_change(true)
		t.eq(editor._layers._rows.size(), 2, "展开时列表有容器和子层两行")

		editor._layers._folded[String(group.get("id", ""))] = true
		editor._layers.refresh()
		t.eq(editor._layers._rows.size(), 1, "折叠后只剩容器那一行")

		tree.root.remove_child(editor)
		editor.free()
	)


# ---------------------------------------------------------------- 贝塞尔运动路径

func _register_motion(t) -> void:
	t.case("motion_samples_bezier_curve", func() -> void:
		var layer := Layer.make_color("块", Color.WHITE)
		layer["motion"] = {
			"enabled": true,
			"start": 0.0,
			"end": 2.0,
			"points": [
				{"p": [0.0, 0.0], "in": [0.0, 0.0], "out": [1.0, 0.0]},
				{"p": [1.0, 1.0], "in": [1.0, 1.0], "out": [1.0, 1.0]},
			],
		}
		layer = Layer.normalize(layer)
		t.eq(Motion.enabled(layer), true, "两点路径可用")

		var p0 = Motion.sample(layer, 0.0)
		t.eq(Vector2(float(p0.x), float(p0.y)), Vector2(0.0, 0.0), "起点")
		var p2 = Motion.sample(layer, 2.0)
		t.eq(Vector2(float(p2.x), float(p2.y)), Vector2(1.0, 1.0), "终点")
		# 中点：控制柄把曲线拉向 x 大的一侧，所以不是直线中点 (0.5, 0.5)。
		var pm = Motion.sample(layer, 1.0)
		t.approx(float(pm.x), 0.875, 0.001, "贝塞尔中点的 x（不是线性中点）")
		t.approx(float(pm.y), 0.5, 0.001, "贝塞尔中点的 y")
		# 越界保持端点值。
		var pneg = Motion.sample(layer, -5.0)
		t.eq(Vector2(float(pneg.x), float(pneg.y)), Vector2(0.0, 0.0), "首帧之前取首值")
		var pover = Motion.sample(layer, 9.0)
		t.eq(Vector2(float(pover.x), float(pover.y)), Vector2(1.0, 1.0), "末帧之后保持末值")
	)

	t.case("motion_takes_over_xy_channels", func() -> void:
		# 路径启用时它接管 X/Y：两条一维轨道描述不了曲线，同时用只会互相打架。
		var layer := Layer.make_color("块", Color.WHITE)
		layer["transform"]["w"] = 100.0
		layer["transform"]["h"] = 100.0
		Timeline.set_key(layer, "x", 0.0, 0.9)      # 会被路径盖掉
		layer["motion"] = {
			"enabled": true, "start": 0.0, "end": 2.0,
			"points": [
				{"p": [0.0, 0.0], "in": [0.0, 0.0], "out": [0.0, 0.0]},
				{"p": [1.0, 0.0], "in": [1.0, 0.0], "out": [1.0, 0.0]},
			],
		}
		layer = Layer.normalize(layer)
		var pose := Timeline.sample_layer(layer, 1.0, Vector2(1000, 500))
		t.approx(float(pose["x"]), 0.5, 0.001, "x 走路径（关键帧的 0.9 被接管）")
		t.approx(float(pose["y"]), 0.0, 0.001, "y 走路径")

		Motion.disable(layer)
		pose = Timeline.sample_layer(layer, 1.0, Vector2(1000, 500))
		t.approx(float(pose["x"]), 0.9, 0.001, "关掉路径后回落关键帧")
	)

	t.case("motion_control_nodes_move_smoothly", func() -> void:
		var layer := Layer.make_color("块", Color.WHITE)
		layer["transform"]["x"] = 0.0
		layer["transform"]["y"] = 0.0
		layer["transform"]["w"] = 200.0
		layer["transform"]["h"] = 100.0
		Motion.enable(layer, Vector2(1280, 720))
		t.eq(Motion.points_of(layer).size(), 2, "默认路径两点")

		# 拖第 2 个节点（第一个点的 out 柄）：对面的 in 柄镜像跟着转。
		var nodes: Array = Motion.control_nodes(layer, Vector2(1280, 720))
		t.ok(nodes.size() >= 3, "控制点列表里有锚点和控制柄")
		var out_node: Dictionary = nodes[1]
		t.eq(String(out_node.get("kind", "")), "out", "第二个节点是 out 柄")
		Motion.move_node(layer, 1, Vector2(0.1, 0.0))
		var pt: Dictionary = Motion.point(layer, 0)
		var out_v := Motion._vec_at(pt, "out")
		var in_v := Motion._vec_at(pt, "in")
		var anchor_v := Motion._vec_at(pt, "p")
		t.approx((out_v - anchor_v).x, -(in_v - anchor_v).x, 0.0001, "两侧控制柄对称")
		t.approx((out_v - anchor_v).y, -(in_v - anchor_v).y, 0.0001, "对称也作用在 y 上")

		Motion.add_point(layer, Vector2(1280, 720))
		t.eq(Motion.points_of(layer).size(), 3, "加了一个路径点")

		# 烘焙：2 秒 @ 60Hz 应该有上百个采样点。
		var baked: Array = Motion.bake(layer, 60.0, 600)
		t.ok(baked.size() > 100, "路径被烘焙成密集关键帧（%d 个）" % baked.size())
	)

	t.case("motion_survives_document_roundtrip", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := Layer.make_color("块", Color.WHITE)
		layer["transform"]["w"] = 100.0
		layer["transform"]["h"] = 100.0
		Motion.enable(layer, Vector2(doc.canvas))
		Motion.add_point(layer, Vector2(doc.canvas))
		doc.add_layer(layer)

		var text: String = doc.to_text()
		t.ok(text.contains("\"motion\""), "路径写进了 JSON")
		var parsed: Dictionary = StudioDoc.parse(text)
		t.eq(parsed.ok, true, "带路径的文档能读回来：" + String(parsed.error))
		if parsed.ok:
			var back: Dictionary = parsed.doc.layers[0]
			t.eq(Motion.enabled(back), true, "读回来仍然启用")
			t.eq(Motion.points_of(back).size(), 3, "路径点数量一致")
			# 路径结束时刻参与"超出循环时长"的判断。
			parsed.doc.loop_seconds = 1.0
			t.approx(float(parsed.doc.key_end()), 4.0, 0.001, "路径的 end 也算末帧")
			t.eq(parsed.doc.out_of_range_keys().is_empty(), true,
					"路径不算关键帧通道（另有提示）")
	)

	t.case("motion_remaps_when_parent_changes", func() -> void:
		# 拖进容器：父级从画布变成容器框，路径点必须跟着换算，否则画面会跳。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := Layer.make_color("块", Color.WHITE)
		layer["transform"]["x"] = 400.0
		layer["transform"]["y"] = 0.0
		layer["transform"]["w"] = 200.0
		layer["transform"]["h"] = 100.0
		Motion.enable(layer, Vector2(doc.canvas))
		doc.add_layer(layer)
		var group := Layer.make_group("容器", 400.0, 200.0)
		group["transform"]["x"] = 400.0
		group["transform"]["y"] = 0.0
		doc.add_layer(group)

		# 记下搬之前 1 秒时的屏幕位置（容器还没有子层，先按根层算）。
		var before = Motion.sample(doc.layers[0], 1.0)
		var before_px := Vector2(float(before.x) * 1280.0, float(before.y) * 720.0)

		var path: PackedInt32Array = doc.reparent(PackedInt32Array([0]),
				PackedInt32Array([1]))
		var moved: Dictionary = doc.layer_at(path)
		var after = Motion.sample(moved, 1.0)
		var after_px := Vector2(float(after.x) * 400.0, float(after.y) * 200.0)
		# 容器原点在 (400,0)，所以"画布上的位置"= 容器位置 + 容器内位置。
		var screen := Vector2(400.0, 0.0) + after_px
		t.ok(screen.distance_to(before_px) < 1.0,
				"进容器后画面不跳（差 %.2f）" % screen.distance_to(before_px))
	)
