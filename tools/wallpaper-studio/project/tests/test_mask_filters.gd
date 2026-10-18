extends RefCounted
## 遮罩层 / 滤镜层的纯函数用例。
##
## 这两块的"算错"很难在窗口里看出来（羽化差一点点、遮罩 UV 差 0.1 都像"就是这样"），
## 所以规则全部钉在这里：遮罩框的 UV 换算、形状编号、覆盖层数、
## 滤镜默认值与着色器里的开关。

const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")


func register(t) -> void:
	t.case("mask_layer_defaults_cover_parent", func() -> void:
		var m := Layer.make_mask("遮罩")
		t.eq(Mask.shape_code(m), 0, "默认矩形 = 0")
		t.eq(Frame.of(m, Vector2(640, 360)), Rect2(Vector2.ZERO, Vector2(640, 360)),
				"宽高为 0 = 覆盖整个父级")
		t.eq(Mask.uv_rect(m, Vector2(640, 360)), Rect2(Vector2.ZERO, Vector2(1, 1)),
				"整块区域的 UV 是 0..1")
		t.eq(Layer.cover_count(m), 0, "默认覆盖下方全部")
	)

	t.case("mask_shape_codes", func() -> void:
		t.eq(Mask.shape_code({"shape": "rect"}), 0, "矩形 0")
		t.eq(Mask.shape_code({"shape": "rounded"}), 1, "圆角 1")
		t.eq(Mask.shape_code({"shape": "ellipse"}), 2, "椭圆 2")
		t.eq(Mask.shape_code({"shape": "image"}), 3, "图片 3")
		t.eq(Mask.shape_code({"shape": "wat"}), 0, "非法形状退回矩形")
	)

	t.case("mask_uv_rect_uses_parent_frame", func() -> void:
		# 遮罩框存在图层自己的 transform 里，坐标相对**父级**（画布 1280x720）。
		var m := Layer.make_mask("遮罩")
		m["transform"]["x"] = 100.0
		m["transform"]["y"] = 50.0
		m["transform"]["w"] = 200.0
		m["transform"]["h"] = 100.0
		var uv := Mask.uv_rect(m, Vector2(1280, 720))
		t.approx(uv.position.x, 100.0 / 1280.0, 0.0001, "左边界在父级 UV 里")
		t.approx(uv.position.y, 50.0 / 720.0, 0.0001, "上边界在父级 UV 里")
		t.approx(uv.size.x, 200.0 / 1280.0, 0.0001, "宽在父级 UV 里")
		t.approx(uv.size.y, 100.0 / 720.0, 0.0001, "高在父级 UV 里")
	)

	t.case("cover_count_normalizes", func() -> void:
		var m := Layer.make_mask("遮罩")
		t.eq(Layer.cover_count(m), 0, "all -> 0（下方全部）")
		m["cover"] = {"mode": "count", "count": 3}
		t.eq(Layer.cover_count(m), 3, "count 原样")
		m["cover"] = {"mode": "count", "count": 0}
		t.eq(Layer.cover_count(m), 1, "至少 1 层")
		m["cover"] = {"mode": "wat", "count": 5}
		t.eq(Layer.cover_count(m), 0, "非法模式退回 all")
	)

	t.case("filters_defaults_are_identity", func() -> void:
		var f := Filters.normalize({})
		t.eq(Filters.on(f, Filters.COLOR), false, "默认全关")
		t.eq(Filters.any(f), false, "any() 也认全关")
		t.eq(Filters.needs_material(f), false, "全关不需要材质")
		t.eq(Filters.blur_radius(f), 0.0, "没开模糊就没有半径")
		t.eq(Filters.needs_copy(f), false, "全关不需要副本")
	)

	t.case("filters_any_detects_enabled_group", func() -> void:
		var f := Filters.normalize({"vignette": {"enabled": true}})
		t.eq(Filters.any(f), true, "任意一组开着就算启用")
	)

	t.case("filters_normalize_clamps_values", func() -> void:
		var f := Filters.normalize({
			"posterize": {"enabled": true, "levels": 1.0},
			"blur": {"enabled": true, "radius": -5.0},
			"color": {"enabled": true, "brightness": 0.5, "contrast": 2.0},
		})
		t.eq(float(f[Filters.POSTERIZE]["levels"]), 2.0, "级数下限 2")
		t.eq(float(f[Filters.BLUR]["radius"]), 0.0, "半径不能为负")
		t.eq(Filters.blur_radius(f), 0.0, "半径 0 = 不模糊")
		t.eq(Filters.needs_material(f), true, "色彩开着就需要材质")
	)

	t.case("filters_grade_uniforms_hue_in_radians", func() -> void:
		var f := Filters.normalize({"color": {"enabled": true, "hue": 90.0, "brightness": 0.1}})
		var u := Filters.grade_uniforms(f, Rect2(Vector2(0, 0), Vector2(1, 1)))
		t.approx(float(u["hue"]), PI * 0.5, 0.0001, "色相按弧度传给着色器")
		t.approx(float(u["brightness"]), 0.1, 0.0001, "亮度原样传")
		t.approx(float(u["contrast"]), 1.0, 0.0001, "没设的参数保持恒等")
	)

	t.case("layer_shader_uses_uv_not_screen_uv", func() -> void:
		# 合成节点是子图里的一块贴图，SCREEN_UV 会跟着外层 viewport 跑（嵌套时遮罩错位），
		# 所以着色器必须用 UV（合成区域自己的 0..1）。
		var code := Filters.layer_shader("mix")
		t.ok(code.contains("vec2 cuv = UV;"), "遮罩坐标来自 UV")
		t.ok(not code.contains("vec2 cuv = screen_origin"), "不再用 SCREEN_UV 拼坐标")
		t.ok(code.contains("render_mode blend_mix;"), "混合模式仍在 render_mode 里")
		t.ok(Filters.blur_shader().contains("blur_step"), "模糊着色器有步长 uniform")
	)
