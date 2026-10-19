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
const Timeline = preload("res://src/doc/timeline.gd")


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

	t.case("editor_inspector_builds_effect_layer_sections", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		var m := Layer.make_mask("遮罩")
		editor.doc.add_layer(m)
		var fl := Layer.make_filter("滤镜")
		fl["filters"][Filters.BLUR]["enabled"] = true
		editor.doc.add_layer(fl)
		editor.apply_change(true)

		editor.select_layer(1)
		t.eq(String(editor.selected_layer().get("type", "")), "mask", "选中遮罩层")
		var mask_sections: int = editor._inspector._box.get_child_count()
		t.ok(mask_sections > 10, "遮罩层面板建了一堆控件（形状/边框/羽化…）")
		editor._inspector.refresh(true)
		t.eq(editor._inspector._box.get_child_count(), mask_sections, "面板段落数稳定")
		t.ok(_node_of(editor, m) != null, "遮罩层在画布树里有合成节点")

		editor.select_layer(2)
		t.eq(String(editor.selected_layer().get("type", "")), "filter", "选中滤镜层")
		t.ok(editor._inspector._box.get_child_count() > 10, "滤镜层面板建了一堆控件")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_add_effect_above_selected_layer", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		editor.doc.add_layer(Layer.make_text("标题", "你好"))
		editor.apply_change(true)
		editor.select_layer(0)
		editor._layers._add_effect(Layer.make_mask("遮罩层"))
		# 新层紧贴在选中层上面，并**绑定**到那一层（不是"按离我多远"数层数）。
		t.eq(editor.doc.layers.size(), 3, "多了一个图层")
		t.eq(String(editor.doc.layers[1]["type"]), "mask", "遮罩层插在底图上方")
		t.eq(Layer.cover_target(editor.doc.layers[1]), String(editor.doc.layers[0]["id"]),
				"绑定到刚选中的图层")
		t.eq(editor.doc.covered_count(PackedInt32Array([1])), 1, "实际覆盖 1 层")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_cover_target_picker_binds_layer", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("A", Color.RED))
		editor.doc.add_layer(Layer.make_color("B", Color.GREEN))
		var fl := Layer.make_filter("滤镜")
		fl["filters"][Filters.COLOR]["enabled"] = true
		editor.doc.add_layer(fl)
		editor.apply_change(true)
		editor.select_layer(2)

		# 「绑定图层」下拉框：条目 = 未选择 + 可绑定的图层（这里 A、B 两层）。
		var pick := _find_option(editor._inspector._box, 3)
		t.ok(pick != null, "属性面板里有「绑定图层」下拉框")
		if pick == null:
			tree.root.remove_child(editor)
			editor.free()
			return
		pick.item_selected.emit(1)                 # 列表自上而下：B 排在前面
		var cover: Dictionary = editor.doc.layers[2]["cover"]
		t.eq(String(cover["mode"]), "layer", "自动切成绑定模式")
		t.eq(String(cover["target"]), String(editor.doc.layers[1]["id"]), "绑到 B")
		t.eq(editor.doc.covered_count(PackedInt32Array([2])), 1, "只覆盖 B 一层")
		t.ok(is_instance_valid(pick), "重建面板时不能当场释放发信号的控件")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_cover_option_rebuild_does_not_free_emitter", func() -> void:
		# 回归：属性面板里的下拉框/数字框在信号回调里会触发整面板重建。
		# 如果重建时把"正在发信号的控件"当场 free 掉，引擎会报
		# "Object ... was freed or unreferenced while a signal is being emitted from it"。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		editor.doc.add_layer(Layer.make_color("中", Color.GRAY))
		var fl := Layer.make_filter("滤镜")
		fl["filters"][Filters.BLUR]["enabled"] = true
		editor.doc.add_layer(fl)
		editor.apply_change(true)
		editor.select_layer(2)

		var option := _find_option(editor._inspector._box, 4)
		t.ok(option != null, "属性面板里有「覆盖」下拉框")
		if option == null:
			tree.root.remove_child(editor)
			editor.free()
			return
		var cover: Dictionary = editor.doc.layers[2]["cover"]
		option.item_selected.emit(2)                    # -> 指定区间
		t.eq(String(cover["mode"]), "range", "切换成「指定区间」")
		t.ok(is_instance_valid(option), "重建时不能当场释放发信号的控件")
		t.ok(editor._inspector._box.get_child_count() > 5, "面板已重建完成")
		t.eq(editor.doc.covered_count(PackedInt32Array([2])), 2, "默认区间覆盖下方全部")

		tree.root.remove_child(editor)
		editor.free()
	)


	t.case("editor_canvas_click_selects_mask_not_filter", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_color("底", Color.BLACK))
		editor.doc.add_layer(Layer.make_mask("遮罩"))
		var fl := Layer.make_filter("滤镜")
		fl["filters"][Filters.COLOR]["enabled"] = true
		editor.doc.add_layer(fl)
		editor.apply_change(true)
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()

		var center: Vector2 = editor._canvas._container.position \
				+ Vector2(editor.doc.canvas) * 0.5 * editor._canvas._scale
		# 滤镜层没有自己的框：点画布跳过它，选到下面的遮罩层。
		t.eq(editor._canvas.layer_at_screen(center), PackedInt32Array([1]), "点中遮罩层")

		editor.select_layer(2)
		t.eq(editor._canvas._selection.visible, true, "选中滤镜层后选区层仍接管点击")
		t.eq(editor.doc.covered_count(PackedInt32Array([2])), 2, "滤镜覆盖下方两层")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_filter_frame_is_draggable_like_a_layer", func() -> void:
		# 用户报的"滤镜层没法像图片一样调位置大小"：滤镜层的框一直存在，
		# 不需要勾任何开关；没固定时框跟着被覆盖内容走，第一次拖/改数字就固定范围。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		var base := Layer.make_color("底", Color.BLACK)
		base["transform"]["x"] = 320.0
		base["transform"]["y"] = 180.0
		base["transform"]["w"] = 640.0
		base["transform"]["h"] = 360.0
		editor.doc.add_layer(base)
		var fl := Layer.make_filter("滤镜")
		fl["filters"][Filters.BLUR]["enabled"] = true
		editor.doc.add_layer(fl)
		editor.apply_change(true)
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()
		editor.select_layer(1)
		var path := PackedInt32Array([1])
		t.ok(_find_label(editor._inspector._box, "跟随被覆盖图层") != null,
				"面板写明框现在跟随被覆盖图层")

		# 框从第一眼起就在，并且等于被覆盖内容的包围盒（跟随状态）。
		t.eq(editor.doc.filter_frame(path), Rect2(Vector2(320, 180), Vector2(640, 360)),
				"滤镜层的框 = 被覆盖内容的包围盒")
		t.eq(editor._canvas._selection._frameable(editor.doc.layers[1]), true,
				"没固定范围也能在画布上拖框")
		t.eq((editor._canvas._selection._space()["rect"] as Rect2).size, Vector2(640, 360),
				"选区框画在包围盒上")

		# 画布点选优先内容层：滤镜层的框不抢下面内容的点击。
		var center: Vector2 = editor._canvas._container.position \
				+ Vector2(editor.doc.canvas) * 0.5 * editor._canvas._scale
		t.eq(editor._canvas.layer_at_screen(center), PackedInt32Array([0]),
				"点画布选中内容，不是覆盖在上面的滤镜层")

		# 在框上按下 = 开始拖，瞬间落成"固定范围"，框的位置不跳。
		t.eq(editor._canvas._selection._try_start_drag(center), true, "框上按下能开始拖")
		t.eq(Filters.region_enabled(editor.doc.layers[1]), true, "第一次拖就固定范围")
		t.eq(Frame.of(editor.doc.layers[1], editor.doc.canvas),
				Rect2(Vector2(320, 180), Vector2(640, 360)), "框落成包围盒，不跳")
		editor._canvas._selection._end_drag()

		# 固定之后场景里就有区域 uniform；拖动只刷新它，不重建节点。
		var node := _node_of(editor, editor.doc.layers[1])
		t.ok(node != null, "滤镜层在画布树里有合成节点")
		var mat = node.material if node != null else null
		t.ok(mat is ShaderMaterial, "区域混合材质")
		if mat is ShaderMaterial:
			t.eq(int((mat as ShaderMaterial).get_shader_parameter("region_shape")), 0,
					"固定范围后 region_shape 生效")
		Frame.move(editor.doc.layers[1], editor.doc.canvas, Vector2(100.0, 0.0))
		editor.apply_live()
		t.ok(_node_of(editor, editor.doc.layers[1]) == node, "拖动不重建节点")
		if mat is ShaderMaterial:
			var rect: Vector4 = (mat as ShaderMaterial).get_shader_parameter("region_rect")
			t.approx(rect.x, 420.0 / 1280.0, 0.0001, "拖动后 region_rect 立刻更新")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_timeline_adds_keys_and_plays", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.add_layer(Layer.make_text("标题", "hi"))
		editor.apply_change(true)
		editor.select_layer(0)
		t.ok(editor._timeline != null, "时间轴面板已装配")
		t.eq(editor._timeline._track.main, editor, "轨道区拿到 main（装配顺序）")
		# 左侧：标尺占位 + 5 条通道行（选中内容图层时）。
		t.eq(editor._timeline._left.get_child_count(), 6, "五条通道 + 标尺占位")

		# 在播放头 1s 加一个 x 关键帧：值 = 当前采样值（基础 x），所以画面不跳。
		editor.seek_time(1.0)
		editor._timeline._add_key("x")
		var track := Timeline.find_track(Timeline.tracks_of(editor.doc.layers[0]), "x")
		t.eq((track["keys"] as Array).size(), 1, "关键帧写进文档")
		if (track["keys"] as Array).size() == 1:
			t.approx(float(track["keys"][0]["t"]), 1.0, 0.0001, "落在播放头")
			t.approx(float(track["keys"][0]["v"]), 0.0, 0.0001, "取基础 x")

		# 时间轴右边界跟着文档走；越界 seek 被夹住。
		editor.seek_time(999.0)
		t.approx(editor._canvas.time, editor.doc.timeline_end(), 0.0001, "seek 夹到末端")

		# 播放/暂停只改状态，不重建节点。
		var node: Control = editor._canvas._root.find_child(
				"L_" + String(editor.doc.layers[0]["id"]), true, false)
		editor._canvas.play()
		t.eq(editor._canvas.playing, true, "开始播放")
		editor._canvas.pause()
		t.eq(editor._canvas.playing, false, "暂停")
		t.ok(node != null and is_instance_valid(node), "播放不重建节点")

		# 收起/展开：不占画布高度，但通道行还在（展开后原样回来）。
		editor._timeline._toggle_collapse()
		t.eq(editor._timeline._body.visible, false, "收起只留标题行")
		t.eq(editor._timeline.custom_minimum_size.y, 30.0, "收起后高度 = 标题行")
		editor._timeline._toggle_collapse()
		t.eq(editor._timeline._body.visible, true, "再点展开")

		# 选中遮罩层：时间轴只给提示，不给 5 条通道（效果层没有节点几何）。
		editor.doc.add_layer(Layer.make_mask("遮罩"))
		editor.apply_change(true)
		editor.select_layer(1)
		t.eq(editor._timeline._left.get_child_count(), 2, "效果层：标尺占位 + 提示")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_drag_writes_key_at_playhead", func() -> void:
		# 拖一个"已经有关键帧"的图层 = 在播放头写关键帧（自动关键帧）。
		# 不这么做的话，拖完 apply_time 会把节点拉回曲线上的值，看起来像拖不动。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		var layer := Layer.make_text("标题", "hi")
		layer["transform"]["x"] = 0.0
		layer["transform"]["w"] = 200.0
		layer["transform"]["h"] = 100.0
		Timeline.set_key(layer, "x", 0.0, 0.0)
		Timeline.set_key(layer, "x", 2.0, 200.0 / 1280.0)
		editor.doc.add_layer(layer)
		editor.apply_change(true)
		editor.select_layer(0)
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()
		editor.seek_time(1.0)

		Frame.move(editor.doc.layers[0], editor.doc.canvas, Vector2(40.0, 0.0))
		editor.apply_live()

		var track := Timeline.find_track(Timeline.tracks_of(editor.doc.layers[0]), "x")
		t.eq((track["keys"] as Array).size(), 3, "播放头多了一个关键帧")
		var found := false
		for k in track["keys"]:
			if absf(float(k["t"]) - 1.0) < 0.0001:
				found = true
				t.approx(float(k["v"]), 40.0 / 1280.0, 0.0001, "关键帧值 = 拖动后的位置（比例）")
		t.eq(found, true, "关键帧落在 1 秒")

		var node: Control = editor._canvas._root.find_child(
				"L_" + String(editor.doc.layers[0]["id"]), true, false)
		t.ok(node != null, "节点存在")
		if node != null:
			t.approx(node.anchor_left, 40.0 / 1280.0, 0.0005, "画布停在拖动后的位置")

		# 属性面板改同一个通道：同样是改播放头那一帧；没有轨道的通道不凭空建 key。
		editor._inspector._auto_key(editor.doc.layers[0], "rotation", 45.0)
		t.eq(Timeline.find_track(Timeline.tracks_of(editor.doc.layers[0]), "rotation"), {},
				"没轨道的通道不自动加 key")
		editor._inspector._auto_key(editor.doc.layers[0], "x", 77.0)
		for k in track["keys"]:
			if absf(float(k["t"]) - 1.0) < 0.0001:
				t.approx(float(k["v"]), 77.0 / 1280.0, 0.0001, "属性面板改值写进播放头那一帧")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_selection_follows_animation", func() -> void:
		# 播放头移动时，选区框与命中测试都必须按"画面上的位置"算，
		# 否则框停在基础位置，而元素已经动到别处了。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		var layer := Layer.make_text("标题", "hi")
		layer["transform"]["x"] = 0.0
		layer["transform"]["y"] = 0.0
		layer["transform"]["w"] = 200.0
		layer["transform"]["h"] = 100.0
		Timeline.set_key(layer, "x", 0.0, 0.0)
		Timeline.set_key(layer, "x", 2.0, 200.0 / 1280.0)
		editor.doc.add_layer(layer)
		editor.apply_change(true)
		editor.select_layer(0)
		editor._canvas.size = Vector2(800, 600)
		editor._canvas._fit()

		editor.seek_time(1.0)
		var rect: Rect2 = editor._canvas._selection._space()["rect"]
		t.approx(rect.position.x, 100.0, 0.01, "选区框跟着关键帧走")

		# 在动画后的位置点一下：命中同一层（基础位置在 0，点 100 也该中）。
		var screen: Vector2 = editor._canvas._container.position \
				+ Vector2(150.0, 50.0) * editor._canvas._scale
		t.eq(editor._canvas.layer_at_screen(screen), PackedInt32Array([0]),
				"命中测试按动画后的框算")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_inspector_shows_playhead_values", func() -> void:
		# 属性面板现在是"时间感知"的：有关键帧的通道显示播放头处的值，
		# 改动写进当前帧；没有关键帧的通道仍改基础值。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		var layer := Layer.make_text("标题", "hi")
		layer["transform"]["w"] = 200.0
		layer["transform"]["h"] = 100.0
		Timeline.set_key(layer, "x", 0.0, 0.25)     # 320px
		Timeline.set_key(layer, "x", 2.0, 0.75)     # 960px
		editor.doc.add_layer(layer)
		editor.apply_change(true)
		editor.select_layer(0)

		var x_ctl := _time_control(editor, "x")
		t.ok(x_ctl != null, "X 控件登记为时间感知")
		if x_ctl == null:
			tree.root.remove_child(editor)
			editor.free()
			return
		editor.seek_time(1.0)
		t.approx(x_ctl.value, 640.0, 0.01, "播放头 1s 显示采样值（像素）")
		editor.seek_time(0.0)
		t.approx(x_ctl.value, 320.0, 0.01, "0s 显示首帧")
		editor.seek_time(2.0)
		t.approx(x_ctl.value, 960.0, 0.01, "2s 显示末帧")

		# 改数字 = 在播放头写关键帧（不是改基础值）。
		editor.seek_time(1.0)
		x_ctl.value = 700.0
		var track := Timeline.find_track(Timeline.tracks_of(editor.doc.layers[0]), "x")
		var found := false
		for k in track["keys"]:
			if absf(float(k["t"]) - 1.0) < 0.0001:
				found = true
				t.approx(float(k["v"]), 700.0 / 1280.0, 0.0001, "新值写进播放头那一帧")
		t.eq(found, true, "播放头处有 key")
		var node: Control = editor._canvas._root.find_child(
				"L_" + String(editor.doc.layers[0]["id"]), true, false)
		if node != null:
			t.approx(node.anchor_left, 700.0 / 1280.0, 0.0005, "画布同步到新值")

		# 缩放行：改数字会建出 scale 轨道，节点真的缩放。
		var scale_ctl := _time_control(editor, "scale")
		t.ok(scale_ctl != null, "有缩放控件")
		if scale_ctl != null:
			scale_ctl.value = 2.0
			var st := Timeline.find_track(Timeline.tracks_of(editor.doc.layers[0]), "scale")
			t.eq((st["keys"] as Array).size(), 1, "缩放写成一个关键帧")
			var node2: Control = editor._canvas._root.find_child(
					"L_" + String(editor.doc.layers[0]["id"]), true, false)
			if node2 != null:
				t.approx(node2.scale.x, 2.0, 0.0005, "节点按缩放动画变大")

		tree.root.remove_child(editor)
		editor.free()
	)

	t.case("editor_timeline_flags_and_fixes_loop", func() -> void:
		# 时长比末帧短 -> 提示 + 一键适配末帧；首尾接不上 -> 一键闭合。
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var editor = load("res://main.tscn").instantiate()
		tree.root.add_child(editor)

		editor.doc.canvas = Vector2i(1280, 720)
		editor.doc.loop_seconds = 1.5
		var layer := Layer.make_text("标题", "hi")
		Timeline.set_key(layer, "x", 0.0, 0.1)
		Timeline.set_key(layer, "x", 3.0, 0.3)
		editor.doc.add_layer(layer)
		editor.apply_change(true)
		editor.select_layer(0)

		editor._timeline.refresh_warnings()
		t.eq(editor._timeline._btn_fit.visible, true, "有超范围关键帧时给出「适配末帧」")
		t.ok(String(editor._timeline._status.text).contains("超出时长"), "状态栏说明原因")

		editor._timeline._on_fit_length()
		t.approx(editor.doc.loop_seconds, 3.0, 0.0001, "适配末帧把时长拉到 3s")
		t.eq(editor._timeline._btn_fit.visible, false, "超范围清空")
		t.eq(editor._timeline._btn_close.visible, true, "首尾不一致时给出「闭合循环」")

		editor._timeline._on_close_loop()
		t.eq(editor._timeline._btn_close.visible, false, "闭合后接缝消失")
		t.eq(bool(editor.doc.loop_seam()["ok"]), true, "接缝检测通过")
		var track := Timeline.find_track(Timeline.tracks_of(editor.doc.layers[0]), "x")
		var found := false
		for k in track["keys"]:
			if absf(float(k["t"]) - 3.0) < 0.0001:
				found = true
				t.approx(float(k["v"]), 0.1, 0.0001, "闭合帧取 0 时刻的值")
		t.eq(found, true, "循环终点补了关键帧")

		tree.root.remove_child(editor)
		editor.free()
	)

## 按通道名找属性面板里那个"时间感知"的数字框。
func _time_control(editor, channel: String) -> SpinBox:
	for item in editor._inspector._time_controls:
		if String(item["channel"]) == channel:
			return item["node"] as SpinBox
	return null


## 在面板里找一个条目数为 count 的 OptionButton（用来点那些会触发面板重建的下拉框）。
func _find_option(node: Node, count: int) -> OptionButton:
	for c in node.get_children():
		if c is OptionButton and (c as OptionButton).item_count == count:
			return c as OptionButton
		var found := _find_option(c, count)
		if found != null:
			return found
	return null


## 按文字找标签（用来确认面板当前写的是"跟随被覆盖图层"还是"固定范围"）。
func _find_label(node: Node, fragment: String) -> Label:
	for c in node.get_children():
		if c is Label and String((c as Label).text).contains(fragment):
			return c as Label
		var found := _find_label(c, fragment)
		if found != null:
			return found
	return null
