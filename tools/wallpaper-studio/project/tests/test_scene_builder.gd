extends RefCounted
## 场景构造的用例。重点验证"预览与导出走同一条构造路径"这条不变式的前半段：
## 文档 -> 节点树的结构正确。
##
## 注意：Node 不会被自动释放，每个用例末尾要 free()，否则引擎退出时会刷泄漏。

const StudioDoc = preload("res://src/doc/doc.gd")
const SceneBuilder = preload("res://src/build/scene_builder.gd")
const Layer = preload("res://src/doc/layer.gd")


func _build(doc):
	return SceneBuilder.build(doc, "")


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
		t.eq(String(root.get_child(1).name), "layer_0", "bottom layer first")
		t.eq(String(root.get_child(2).name), "layer_1", "top layer last")
		t.ok(root.get_child(1) is ColorRect, "color layer -> ColorRect")
		t.ok(root.get_child(2) is Label, "text layer -> Label")
		root.free()
	)

	t.case("builder_applies_transform_and_visibility", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("底", Color.BLACK)
		l["transform"] = {"x": 100.0, "y": 25.0, "scale": 2.0, "rotation": 90.0, "opacity": 0.5}
		l["visible"] = false
		doc.add_layer(l)
		var root = _build(doc)
		var n: Control = root.get_child(1)
		t.eq(n.position, Vector2(100, 25), "position applied")
		t.eq(n.scale, Vector2(2, 2), "scale applied")
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
		var n: Control = root.get_child(1)
		t.ok(n.material is CanvasItemMaterial, "add blend gets a material")
		t.eq(n.material.blend_mode, CanvasItemMaterial.BLEND_MODE_ADD, "blend mode set")
		root.free()
	)

	t.case("builder_mix_blend_has_no_material", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var root = _build(doc)
		t.eq(root.get_child(1).material, null, "mix uses no material")
		root.free()
	)

	t.case("builder_injects_clock_script_for_dynamic_text", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_text("时钟", "00:00:00")
		l["dynamic"] = "clock"
		doc.add_layer(l)
		var root = _build(doc)
		var label: Label = root.get_child(1)
		t.ok(label.get_script() != null, "dynamic text gets a script")
		t.eq(label.get("mode"), "clock", "clock mode set")
		root.free()
	)

	t.case("builder_skips_clock_script_for_static_text", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_text("标题", "你好"))
		var root = _build(doc)
		var label: Label = root.get_child(1)
		t.eq(label.get_script(), null, "static text has no script")
		t.eq(label.text, "你好", "static text kept")
		root.free()
	)

	t.case("builder_particles_wrapped_in_control", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_particles("雪", ""))
		var root = _build(doc)
		# 粒子是 Node2D，必须包一层 Control 才能统一走 apply_common
		var wrap: Control = root.get_child(1)
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
		var tr: TextureRect = root.get_child(1)
		t.eq(tr.texture, null, "no texture for a missing file")
		root.free()
	)

	t.case("builder_particles_fall_back_to_builtin_dot", func() -> void:
		# 没选贴图的新粒子图层必须有内置圆点，否则画布上完全看不见，
		# 用户会以为"添加粒子失败"。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_particles("雪", ""))
		var root = _build(doc)
		var particles: GPUParticles2D = root.get_child(1).get_child(0)
		t.ok(particles.texture != null, "default particle texture generated")
		root.free()
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
		for node in [root, root.get_child(0), root.get_child(1)]:
			t.approx(node.anchor_right, 1.0, 0.0001, "anchored to right edge")
			t.approx(node.anchor_bottom, 1.0, 0.0001, "anchored to bottom edge")
			t.eq(node.offset_right, 0.0, "offset_right zeroed")
			t.eq(node.offset_bottom, 0.0, "offset_bottom zeroed")
		root.free()
	)
