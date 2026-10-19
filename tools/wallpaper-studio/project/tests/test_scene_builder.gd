extends RefCounted
## 场景构造的用例。重点验证"预览与导出走同一条构造路径"这条不变式的前半段：
## 文档 -> 节点树的结构正确。
##
## 节点形状：每个内容图层一个 box（名字 L_<id>，锚点 = 框），box 里是 content；
## 效果图层（遮罩/滤镜）把覆盖到的兄弟节点收进一个合成节点（名字也换成 L_<id>），
## 所以"被覆盖的图层"不再是根的直接孩子。断言都按"找名字"来写，
## 不要在用例里写死第几层孩子 —— 那层数已经改过三次了。
##
## 注意：Node 不会被自动释放，每个用例末尾要 free()，否则引擎退出时会刷泄漏。

const StudioDoc = preload("res://src/doc/doc.gd")
const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Layer = preload("res://src/doc/layer.gd")
const Filters = preload("res://src/doc/filters.gd")


func _build(doc):
	return SceneBuilder.build(doc, "")


func _clean_dir(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	for f in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(f))
	for d in DirAccess.get_directories_at(path):
		_clean_dir(path.path_join(d))
	DirAccess.remove_absolute(path)


## 第 i 个根图层对应的 box（root 的第 0 个孩子是背景）。
func _box(root: Node, i: int) -> Control:
	return root.get_child(i + 1)


## 第 i 个根图层的内容节点（纯色/图片/文字/粒子；容器没有 content）。
func _content(root: Node, i: int) -> Node:
	return _box(root, i).find_child("content", true, false)


func register(t) -> void:
	t.case("builder_root_is_full_rect_control", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var root = _build(doc)
		t.ok(root is Control, "root is a Control")
		# 根节点靠"铺满父级"的锚点自适应，而不是固定像素尺寸（见入树用例）。
		t.approx(root.anchor_right, 1.0, 0.0001, "root anchored to the right edge")
		t.approx(root.anchor_bottom, 1.0, 0.0001, "root anchored to the bottom edge")
		t.eq(root.offset_right, 0.0, "root offsets zeroed")
		t.eq(root.get_child_count(), 1, "background only")
		t.ok(root.get_child(0) is ColorRect, "background is a ColorRect")
		root.free()
	)

	t.case("builder_layer_order_matches_document", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		doc.add_layer(Layer.make_text("时钟", "00:00:00"))
		var root = _build(doc)
		t.eq(root.get_child_count(), 3, "background + 2 layers")
		t.eq(String(_box(root, 0).name), "L_" + String(doc.layers[0]["id"]), "bottom layer first")
		t.eq(String(_box(root, 1).name), "L_" + String(doc.layers[1]["id"]), "top layer last")
		t.ok(_content(root, 0) is ColorRect, "color layer -> ColorRect")
		t.ok(_content(root, 1) is Label, "text layer -> Label")
		root.free()
	)

	t.case("builder_applies_transform_and_visibility", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("底", Color.BLACK)
		l["transform"]["x"] = 100.0
		l["transform"]["y"] = 25.0
		l["transform"]["rotation"] = 90.0
		l["transform"]["opacity"] = 0.5
		l["visible"] = false
		doc.add_layer(l)
		var root = _build(doc)
		var n := _box(root, 0)
		# 没定过尺寸的图层铺满父级，位置仍然是 transform 里的偏移。
		t.eq(n.position, Vector2(100, 25), "position applied")
		t.approx(n.rotation_degrees, 90.0, 0.001, "rotation applied")
		t.approx(n.modulate.a, 0.5, 0.001, "opacity applied")
		t.eq(n.visible, false, "visibility applied")
		root.free()
	)

	t.case("builder_applies_blend_mode", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("底", Color.BLACK)
		l["blend"] = "add"
		doc.add_layer(l)
		var root = _build(doc)
		var n := _content(root, 0)
		t.ok(n.material is CanvasItemMaterial, "add blend gets a material")
		t.eq((n.material as CanvasItemMaterial).blend_mode, CanvasItemMaterial.BLEND_MODE_ADD,
				"blend mode set")
		root.free()
	)

	t.case("builder_mix_blend_has_no_material", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var root = _build(doc)
		t.eq(_content(root, 0).material, null, "mix uses no material")
		root.free()
	)

	t.case("builder_injects_clock_script_for_dynamic_text", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_text("时钟", "00:00:00")
		l["dynamic"] = "clock"
		doc.add_layer(l)
		var root = _build(doc)
		var label := _content(root, 0)
		t.ok(label.get_script() != null, "dynamic text gets a script")
		t.eq(label.get("mode"), "clock", "clock mode set")
		root.free()
	)

	t.case("builder_skips_clock_script_for_static_text", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_text("标题", "你好"))
		var root = _build(doc)
		var label := _content(root, 0)
		t.eq(label.get_script(), null, "static text has no script")
		t.eq(String(label.get("text")), "你好", "static text kept")
		root.free()
	)

	t.case("builder_particles_wrapped_in_control", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_particles("雪", ""))
		var root = _build(doc)
		var wrap := _content(root, 0)
		t.ok(wrap is Control, "particles wrapped in a Control")
		t.eq(wrap.get_child_count(), 1, "one child")
		t.ok(wrap.get_child(0) is GPUParticles2D, "inner node is GPUParticles2D")
		t.ok(wrap.get_child(0).process_material is ParticleProcessMaterial, "process material set")
		root.free()
	)

	t.case("builder_no_animation_player_yet", func() -> void:
		# 阶段 1 不生成 AnimationPlayer；阶段 2 会把这条用例改成相反断言。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var root = _build(doc)
		var found := false
		for c in root.get_children():
			if c is AnimationPlayer:
				found = true
		t.eq(found, false, "no AnimationPlayer in stage 1")
		root.free()
	)

	t.case("builder_missing_asset_yields_null_texture", func() -> void:
		# 资源丢了不能让构造崩溃 —— 图层照样建出来，只是没有贴图（导出前校验会拦住）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_image("花瓣", "nope.png"))
		var root = _build(doc)
		t.eq(_content(root, 0).get("texture"), null, "no texture for a missing file")
		root.free()
	)

	t.case("builder_particles_fall_back_to_builtin_dot", func() -> void:
		# 没选贴图的新粒子图层必须有内置圆点，否则画布上完全看不见，
		# 用户会以为"添加粒子失败"。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_particles("雪", ""))
		var root = _build(doc)
		var particles: GPUParticles2D = _content(root, 0).get_child(0)
		t.ok(particles.texture != null, "default particle texture generated")
		root.free()
	)

	t.case("builder_applies_explicit_frame_as_anchors", func() -> void:
		# 显式框用**锚点**表达：导出的壁纸随窗口等比伸缩，而不是钉死在像素尺寸上。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("底", Color.BLACK)
		l["transform"]["x"] = 100.0
		l["transform"]["y"] = 50.0
		l["transform"]["w"] = 640.0
		l["transform"]["h"] = 360.0
		doc.add_layer(l)
		var root = _build(doc)
		var n := _box(root, 0)
		t.approx(n.anchor_left, 100.0 / 1280.0, 0.0001, "left anchor")
		t.approx(n.anchor_top, 50.0 / 720.0, 0.0001, "top anchor")
		t.approx(n.anchor_right, 740.0 / 1280.0, 0.0001, "right anchor")
		t.approx(n.anchor_bottom, 410.0 / 720.0, 0.0001, "bottom anchor")
		t.eq(n.offset_left, 0.0, "offsets zeroed")
		t.eq(n.offset_top, 0.0, "offsets zeroed")
		root.free()
	)

	t.case("builder_text_wraps_only_when_framed", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_text("标题", "很长的一段文字，用来验证折行"))
		var root = _build(doc)
		t.eq(_content(root, 0).get("autowrap_mode"), TextServer.AUTOWRAP_OFF,
				"没拖过尺寸的文本不折行")
		root.free()

		doc.layers[0]["transform"]["w"] = 400.0
		doc.layers[0]["transform"]["h"] = 120.0
		var root2 = _build(doc)
		t.ok(int(_content(root2, 0).get("autowrap_mode")) != TextServer.AUTOWRAP_OFF,
				"显式框的文本按框宽折行")
		root2.free()
	)

	t.case("builder_layers_fill_parent_without_doubling", func() -> void:
		# 回归用例：用 set_anchors_preset 时 offset_right/bottom 会被留成画布尺寸，
		# 进树后 size = anchor*parent + offset = 1920+1920 —— 整张画布放大一倍，
		# 文字和图片全跑出可视区（实测只剩右下角半个色块）。
		# 无头测试里拿不到"入树后的一帧"（布局在下一帧才跑），所以直接钉住不变量：
		# 全屏锚点 + 偏移归零。
		var doc = StudioDoc.create_empty("t", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var root = _build(doc)
		for node in [root, root.get_child(0), _box(root, 0), _content(root, 0)]:
			t.approx(node.anchor_right, 1.0, 0.0001, "anchored to right edge")
			t.approx(node.anchor_bottom, 1.0, 0.0001, "anchored to bottom edge")
			t.eq(node.offset_right, 0.0, "offset_right zeroed")
			t.eq(node.offset_bottom, 0.0, "offset_bottom zeroed")
		root.free()
	)

	# ---------------------------------------------------------------- 容器 / 遮罩 / 滤镜

	t.case("builder_group_nests_children_with_relative_anchors", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var group := Layer.make_group("容器", 640.0, 360.0)
		group["transform"]["x"] = 100.0
		group["transform"]["y"] = 50.0
		var child := Layer.make_color("子", Color.WHITE)
		child["transform"]["x"] = 0.0
		child["transform"]["y"] = 0.0
		child["transform"]["w"] = 320.0
		child["transform"]["h"] = 180.0
		group["children"] = [child]
		doc.add_layer(group)
		var root = _build(doc)
		var box := _box(root, 0)
		var holder: Control = box.find_child("group", true, false)
		t.ok(holder != null, "容器有 group 节点")
		t.eq(holder.clip_contents, true, "容器默认裁剪子元素")
		# 容器的框用画布锚点
		t.approx(box.anchor_right, 740.0 / 1280.0, 0.0001, "容器右锚点")
		var kids := holder.get_children()
		t.eq(kids.size(), 1, "一个子图层")
		var kid := kids[0] as Control
		# 子层的锚点是**相对容器框**的：320/640 = 0.5
		t.approx(kid.anchor_right, 0.5, 0.0001, "子层右锚点相对容器")
		t.approx(kid.anchor_bottom, 0.5, 0.0001, "子层下锚点相对容器")
		root.free()
	)

	t.case("builder_mask_layer_wraps_covered_layers", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("底", Color.WHITE)
		l["transform"]["w"] = 640.0
		l["transform"]["h"] = 360.0
		doc.add_layer(l)
		var m := Layer.make_mask("遮罩")
		m["shape"] = "ellipse"
		m["transform"]["w"] = 320.0
		m["transform"]["h"] = 180.0
		doc.add_layer(m)
		var root = _build(doc)
		# 背景 + 合成节点：被遮罩的那层已经收进合成里，不再是根的直接孩子。
		t.eq(root.get_child_count(), 2, "背景 + 合成节点")
		var comp := root.get_child(1) as Control
		t.eq(String(comp.name), "L_" + String(m["id"]), "合成节点用遮罩层的 id 命名")
		t.ok(comp is SubViewportContainer, "遮罩把内容合成进 SubViewport")
		var mat = comp.material
		t.ok(mat is ShaderMaterial, "遮罩挂上着色器材质")
		t.eq(int((mat as ShaderMaterial).get_shader_parameter("mask_shape")), 2, "椭圆 = 2")
		var rect: Vector4 = (mat as ShaderMaterial).get_shader_parameter("mask_rect")
		# 遮罩框是画布坐标 320x180 / 1280x720。
		t.approx(rect.z, 0.25, 0.0001, "遮罩 UV 宽 = 画布的 1/4")
		t.approx(rect.w, 0.25, 0.0001, "遮罩 UV 高 = 画布的 1/4")
		var covered: Control = comp.find_child("covered", true, false)
		t.ok(covered != null, "合成节点里有被覆盖的图层")
		if covered != null:
			t.eq(covered.get_child_count(), 1, "一层内容进了合成")
		root.free()
	)

	t.case("builder_filter_layer_sets_uniforms", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("调色")
		fl["filters"][Filters.COLOR]["enabled"] = true
		fl["filters"][Filters.COLOR]["brightness"] = 0.25
		doc.add_layer(fl)
		var root = _build(doc)
		var comp := root.get_child(1) as Control
		t.ok(comp is SubViewportContainer, "调色也要先合成")
		t.ok(comp.material is ShaderMaterial, "色彩滤镜挂上着色器材质")
		t.approx(float((comp.material as ShaderMaterial).get_shader_parameter("brightness")),
				0.25, 0.0001, "亮度写进 uniform")
		root.free()
	)

	t.case("builder_filter_layer_blur_wraps_two_viewports", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("模糊")
		fl["filters"][Filters.BLUR]["enabled"] = true
		fl["filters"][Filters.BLUR]["radius"] = 24.0
		doc.add_layer(fl)
		var root = _build(doc)
		var comp := root.get_child(1) as Control
		var inner := comp.find_children("*", "SubViewportContainer", true, false)
		t.eq(inner.size() + (1 if comp is SubViewportContainer else 0), 2,
				"两趟（水平 + 垂直）")
		t.ok(comp.material is ShaderMaterial, "模糊容器有着色器")
		t.ok((comp.material as ShaderMaterial).shader.code.contains("blur_step"),
				"挂的是模糊着色器")
		root.free()
	)

	t.case("builder_filter_shadow_copies_covered_content", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("阴影")
		fl["filters"][Filters.SHADOW]["enabled"] = true
		doc.add_layer(fl)
		var root = _build(doc)
		# 只开阴影时不需要逐像素合成，合成节点就是那个 stack。
		var comp := root.get_child(1) as Control
		t.eq(comp.get_child_count(), 2, "一份阴影副本垫在内容下面")
		t.approx((comp.get_child(0) as Control).modulate.a, 0.45, 0.01, "阴影用 alpha 染色")
		root.free()
	)

	t.case("builder_hidden_effect_layer_does_nothing", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var m := Layer.make_mask("遮罩")
		m["visible"] = false
		doc.add_layer(m)
		var root = _build(doc)
		t.eq(root.get_child_count(), 2, "背景 + 原图层（没有合成节点）")
		t.eq(_box(root, 0).material, null, "隐藏的效果层不留材质")
		root.free()
	)

	t.case("builder_effect_with_nothing_below_is_skipped", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_mask("孤零零的遮罩"))
		var root = _build(doc)
		t.eq(root.get_child_count(), 1, "只有背景")
		root.free()
	)

	t.case("builder_effect_cover_count_limits_layers", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		doc.add_layer(Layer.make_color("中", Color.GRAY))
		doc.add_layer(Layer.make_color("上", Color.WHITE))
		var fl := Layer.make_filter("只盖两层")
		fl["cover"] = {"mode": "count", "count": 2}
		fl["filters"][Filters.POSTERIZE]["enabled"] = true
		doc.add_layer(fl)
		var root = _build(doc)
		# 背景 + 底图 + 合成（中、上两层被覆盖）
		t.eq(root.get_child_count(), 3, "最底下那层没被覆盖")
		var comp := root.get_child(2) as Control
		var covered: Control = comp.find_child("covered", true, false)
		t.eq(covered.get_child_count() if covered != null else 0, 2, "合成里恰好两层")
		root.free()
	)

	t.case("builder_cover_range_keeps_uncovered_layers_on_top", func() -> void:
		# 只盖"第 2~3 层"时，紧挨着效果层下面的第 1 层不受影响，
		# 必须继续画在合成结果**上面**（叠放次序不能被打乱）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("A", Color.RED))
		doc.add_layer(Layer.make_color("B", Color.GREEN))
		doc.add_layer(Layer.make_color("C", Color.BLUE))
		var m := Layer.make_mask("只盖 A、B")
		m["cover"] = {"mode": "range", "from": 2, "to": 3}
		doc.add_layer(m)
		var root = _build(doc)
		t.eq(root.get_child_count(), 3, "背景 + A/B 合成 + C")
		var last: Node = root.get_child(root.get_child_count() - 1)
		t.eq(last.name, "L_" + String(doc.layers[2]["id"]), "没被覆盖的 C 仍在最上层")
		var comp: Node = root.find_child("L_" + String(m["id"]), true, false)
		t.ok(comp != null, "合成节点存在")
		if comp != null:
			var covered: Node = comp.find_child("covered", true, false)
			t.eq(covered.get_child_count() if covered != null else 0, 2,
					"合成里正好 A、B 两层")
			t.eq(comp.get_index(), 1, "合成节点停在覆盖区间原来的位置")
		root.free()
	)

	# ---------------------------------------------------------------- 绑定到指定图层

	t.case("builder_bound_effect_wraps_only_its_target", func() -> void:
		# 绑定模式的关键性质：效果层在自己位置上**不生成任何东西**，
		# 目标那一层就地被包起来，其他图层一点不受影响（不是"整屏套滤镜"）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("A", Color.RED)
		doc.add_layer(a)
		var b := Layer.make_color("B", Color.GREEN)
		doc.add_layer(b)
		var fl := Layer.make_filter("只作用于 A")
		fl["filters"][Filters.VIGNETTE]["enabled"] = true
		fl["cover"] = {"mode": "layer", "target": String(a["id"])}
		doc.add_layer(fl)                    # 加在 B 上面（位置对绑定没有意义）
		var root = _build(doc)
		t.eq(root.get_child_count(), 3, "背景 + A 的合成节点 + B")
		var comp: Control = root.find_child("L_" + String(fl["id"]), true, false)
		t.ok(comp != null, "合成节点用滤镜层的 id 命名")
		if comp != null:
			t.eq(comp.get_index(), 1, "合成节点停在 A 原来的位置")
			var covered: Control = comp.find_child("covered", true, false)
			t.eq(covered.get_child_count() if covered != null else 0, 1, "只包了 A 一层")
		var top: Control = root.get_child(2) as Control
		t.eq(String(top.name), "L_" + String(b["id"]), "B 没被卷进合成")
		root.free()
	)

	t.case("builder_bound_effect_region_follows_target_frame", func() -> void:
		# 暗角这类"按区域算"的滤镜用被覆盖内容的包围盒：绑到右下角一个小块时，
		# layer_uv 应该是那个小块的 UV，而不是整张画布（否则看起来像没生效）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("小块", Color.WHITE)
		a["transform"]["x"] = 640.0
		a["transform"]["y"] = 360.0
		a["transform"]["w"] = 320.0
		a["transform"]["h"] = 180.0
		doc.add_layer(a)
		var fl := Layer.make_filter("暗角")
		fl["filters"][Filters.VIGNETTE]["enabled"] = true
		fl["cover"] = {"mode": "layer", "target": String(a["id"])}
		doc.add_layer(fl)
		var root = _build(doc)
		var comp: Control = root.find_child("L_" + String(fl["id"]), true, false)
		t.ok(comp != null and comp.material is ShaderMaterial, "暗角要材质")
		if comp != null and comp.material is ShaderMaterial:
			var uv: Vector4 = (comp.material as ShaderMaterial).get_shader_parameter("layer_uv")
			t.approx(uv.x, 0.5, 0.0001, "区域左边界 = 元素位置")
			t.approx(uv.y, 0.5, 0.0001, "区域上边界 = 元素位置")
			t.approx(uv.z, 0.25, 0.0001, "区域宽 = 元素宽")
			t.approx(uv.w, 0.25, 0.0001, "区域高 = 元素高")
		root.free()
	)

	t.case("builder_bound_effect_inside_group_wraps_child", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("A", Color.RED)
		var b := Layer.make_color("B", Color.WHITE)
		var group := Layer.make_group("容器", 640.0, 360.0)
		group["children"] = [a, b]
		doc.add_layer(group)
		var fl := Layer.make_filter("只作用于 A")
		fl["filters"][Filters.POSTERIZE]["enabled"] = true
		fl["cover"] = {"mode": "layer", "target": String(a["id"])}
		doc.insert_layer(PackedInt32Array([0]), fl, 2)      # 容器最上层
		var root = _build(doc)
		var holder: Control = _box(root, 0).find_child("group", true, false)
		t.eq(holder.get_child_count(), 2, "容器里：A 的合成节点 + B")
		t.eq(String(holder.get_child(0).name), "L_" + String(fl["id"]), "A 被就地合成")
		t.eq(String(holder.get_child(1).name), "L_" + String(b["id"]), "B 原样留在容器里")
		root.free()
	)

	t.case("builder_bound_effect_without_target_does_nothing", func() -> void:
		# 绑定的图层被删掉 / 从没选过：效果层什么都不做，也不留材质、不崩。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("孤儿滤镜")
		fl["filters"][Filters.BLUR]["enabled"] = true
		fl["cover"] = {"mode": "layer", "target": "l_missing"}
		doc.add_layer(fl)
		var root = _build(doc)
		t.eq(root.get_child_count(), 2, "背景 + 原图层（没有合成节点）")
		t.eq((root.get_child(1) as Control).material, null, "没被卷进合成")
		root.free()
	)

	# ---------------------------------------------------------------- 滤镜的作用区域

	t.case("builder_filter_region_sets_uniforms", func() -> void:
		# 开了作用区域：框写成 region_rect（父级 UV），框外由着色器混回原图。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("只在框内调色")
		fl["filters"][Filters.COLOR]["enabled"] = true
		fl["region"] = {"enabled": true, "shape": "rounded", "radius": 32.0, "feather": 8.0}
		fl["transform"]["x"] = 320.0
		fl["transform"]["y"] = 180.0
		fl["transform"]["w"] = 640.0
		fl["transform"]["h"] = 360.0
		doc.add_layer(fl)
		var root = _build(doc)
		var comp := root.get_child(1) as Control
		t.ok(comp.material is ShaderMaterial, "区域 + 调色 => 有材质")
		var mat := comp.material as ShaderMaterial
		t.eq(int(mat.get_shader_parameter("region_shape")), 1, "圆角 = 1")
		var rect: Vector4 = mat.get_shader_parameter("region_rect")
		t.approx(rect.x, 0.25, 0.0001, "区域左边界在父级 UV 里")
		t.approx(rect.y, 0.25, 0.0001, "区域上边界在父级 UV 里")
		t.approx(rect.z, 0.5, 0.0001, "区域宽在父级 UV 里")
		t.approx(rect.w, 0.5, 0.0001, "区域高在父级 UV 里")
		var radii: Vector2 = mat.get_shader_parameter("region_radii")
		t.approx(radii.x, 32.0 / 640.0, 0.0001, "圆角按框宽归一化")
		var feather: Vector2 = mat.get_shader_parameter("region_feather")
		t.approx(feather.y, 8.0 / 360.0, 0.0001, "羽化按框高归一化")
		t.eq(bool(mat.get_shader_parameter("region_blend")), true, "逐像素滤镜：框外混回原图")
		root.free()
	)

	t.case("builder_filter_region_off_leaves_no_region", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("调色")
		fl["filters"][Filters.COLOR]["enabled"] = true
		doc.add_layer(fl)
		var root = _build(doc)
		var mat := (root.get_child(1) as Control).material as ShaderMaterial
		t.eq(int(mat.get_shader_parameter("region_shape")), -1, "没开区域 = 不限制")
		root.free()
	)

	t.case("builder_filter_region_blur_mixes_with_original", func() -> void:
		# 模糊 + 区域：模糊链里拿不回原图，所以在最外面再包一层区域混合，
		# 原图 viewport 必须在同一个子树里先渲好（顺序见 layer_nodes.wrap_region_mix）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("只在框内模糊")
		fl["filters"][Filters.BLUR]["enabled"] = true
		fl["filters"][Filters.BLUR]["radius"] = 16.0
		fl["region"] = {"enabled": true, "shape": "ellipse"}
		fl["transform"]["w"] = 640.0
		fl["transform"]["h"] = 360.0
		doc.add_layer(fl)
		var root = _build(doc)
		var comp := root.get_child(1) as Control
		t.ok(comp is SubViewportContainer, "最外层是区域混合容器")
		t.ok(comp.material is ShaderMaterial, "区域混合有材质")
		if comp.material is ShaderMaterial:
			t.ok((comp.material as ShaderMaterial).shader.code.contains("base_tex"),
					"挂的是区域混合着色器")
			t.eq(int((comp.material as ShaderMaterial).get_shader_parameter("region_shape")), 2,
					"椭圆 = 2")
		t.ok(comp.find_child("base_viewport", true, false) != null,
				"原图 viewport 在被渲进的那棵子树里")
		root.free()
	)

	t.case("builder_filter_region_clips_copies", func() -> void:
		# 只开阴影时没有逐像素材质；副本整组先合成一张图，再按**画布空间**的区域裁 alpha。
		# （不给每个副本单独挂着色器：那边拿到的 UV 是元素自己的 0..1，区域框会对不上。）
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("框内阴影")
		fl["filters"][Filters.SHADOW]["enabled"] = true
		fl["region"] = {"enabled": true}
		fl["transform"]["w"] = 320.0
		fl["transform"]["h"] = 180.0
		doc.add_layer(fl)
		var root = _build(doc)
		var comp := root.get_child(1) as Control
		t.eq(comp.get_child_count(), 2, "副本合成节点 + 内容")
		var copies := comp.get_child(0) as Control
		t.ok(copies is SubViewportContainer, "副本整组先合成一张图")
		t.ok(copies.material is ShaderMaterial, "区域裁剪材质挂在合成容器上")
		if copies.material is ShaderMaterial:
			var mat := copies.material as ShaderMaterial
			t.eq(bool(mat.get_shader_parameter("region_blend")), false, "副本只裁 alpha")
			t.eq(int(mat.get_shader_parameter("region_shape")), 0, "矩形区域")
		t.ok(copies.find_child("copies", true, false) != null, "阴影副本在合成图里")
		root.free()
	)

	t.case("builder_filter_region_off_keeps_copies_flat", func() -> void:
		# 没开区域时不额外套 viewport：副本还是直接垫在内容下面（老结构，省一趟渲染）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.WHITE))
		var fl := Layer.make_filter("阴影")
		fl["filters"][Filters.SHADOW]["enabled"] = true
		doc.add_layer(fl)
		var root = _build(doc)
		var comp := root.get_child(1) as Control
		t.eq(comp.get_child_count(), 2, "一份副本 + 内容")
		t.ok(not (comp.get_child(0) is SubViewportContainer), "不额外套合成")
		root.free()
	)

	t.case("builder_image_frames_become_animated_texture", func() -> void:
		# 动图：帧序列优先于 src。预览直接建 AnimatedTexture，导出则交给运行时脚本。
		var dir := ProjectSettings.globalize_path("user://test_builder_frames")
		_clean_dir(dir)
		DirAccess.make_dir_recursive_absolute(dir.path_join("assets"))
		var frames := []
		for i in 2:
			var img := Image.create(4, 4, false, Image.FORMAT_RGBA8)
			img.fill(Color(1, 0, 0, 1) if i == 0 else Color(0, 1, 0, 1))
			var rel := "assets/f%d.png" % i
			img.save_png(dir.path_join(rel))
			frames.append(rel)

		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_image("动图", "")
		l["frames"] = frames
		l["fps"] = 8.0
		doc.add_layer(l)
		var root = SceneBuilder.build(doc, dir)
		var content := _content(root, 0)
		t.ok(content is TextureRect, "图片层是 TextureRect")
		if content is TextureRect:
			t.ok(content.texture is AnimatedTexture, "预览纹理是 AnimatedTexture")
			if content.texture is AnimatedTexture:
				var anim := content.texture as AnimatedTexture
				t.eq(anim.frames, 2, "两帧")
				t.approx(anim.fps, 8.0, 0.0001, "帧率来自图层")
		root.free()

		# 导出路径：res:// 下不建纹理，改为把帧列表与帧率写进运行时脚本。
		var root2 = SceneBuilder.build(doc, "res://build/test_pack/assets")
		var content2 := _content(root2, 0)
		t.ok(content2.get_script() != null, "导出路径挂了运行时解码脚本")
		t.eq((content2.get("frames") as PackedStringArray).size(), 2, "帧列表写进脚本")
		t.approx(float(content2.get("fps")), 8.0, 0.0001, "帧率写进脚本")
		t.eq(content2.texture, null, "导出时不内联纹理")
		root2.free()
		_clean_dir(dir)
	)

