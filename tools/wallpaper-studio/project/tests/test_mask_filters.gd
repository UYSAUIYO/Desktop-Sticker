extends RefCounted
## 遮罩与滤镜的纯函数用例。
##
## 这两块的"算错"很难在窗口里看出来（羽化差一点点、遮罩 UV 差 0.1 都像"就是这样"），
## 所以规则全部钉在这里：遮罩矩形/UV 换算、形状编号、滤镜默认值与着色器里的开关。

const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")


func register(t) -> void:
	t.case("mask_default_covers_whole_layer", func() -> void:
		var m := Mask.normalize({})
		t.eq(Mask.rect(m, Vector2(640, 360)), Rect2(Vector2.ZERO, Vector2(640, 360)),
				"宽高为 0 = 覆盖整层")
		t.eq(Mask.shape_code(m), -1, "没启用 -> 不生成遮罩")
	)

	t.case("mask_shape_codes", func() -> void:
		t.eq(Mask.shape_code({"enabled": true, "shape": "rect"}), 0, "矩形 0")
		t.eq(Mask.shape_code({"enabled": true, "shape": "rounded"}), 1, "圆角 1")
		t.eq(Mask.shape_code({"enabled": true, "shape": "ellipse"}), 2, "椭圆 2")
		t.eq(Mask.shape_code({"enabled": true, "shape": "image"}), 3, "图片 3")
		t.eq(Mask.shape_code({"enabled": true, "shape": "wat"}), 0, "非法形状退回矩形")
	)

	t.case("mask_uv_rect_scales_with_layer_frame", func() -> void:
		var m := Mask.normalize({"enabled": true, "x": 100.0, "y": 50.0, "w": 200.0, "h": 100.0})
		# 图层框在画布 1280x720 里占 (0..0.5, 0..0.5)；遮罩是图层的一半。
		var layer_uv := Rect2(Vector2(0.0, 0.0), Vector2(0.5, 0.5))
		var uv := Mask.uv_rect(m, layer_uv, Vector2(400, 200))
		t.approx(uv.position.x, 100.0 / 400.0 * 0.5, 0.0001, "遮罩左边界在画布 UV 里")
		t.approx(uv.size.x, 200.0 / 400.0 * 0.5, 0.0001, "遮罩宽在画布 UV 里")
		t.approx(uv.size.y, 100.0 / 200.0 * 0.5, 0.0001, "遮罩高在画布 UV 里")
	)

	t.case("filters_defaults_are_identity", func() -> void:
		var f := Filters.normalize({})
		t.eq(Filters.on(f, Filters.COLOR), false, "默认全关")
		t.eq(Filters.needs_material(f), false, "全关不需要材质")
		t.eq(Filters.blur_radius(f), 0.0, "没开模糊就没有半径")
		t.eq(Filters.needs_copy(f), false, "全关不需要副本")
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

	t.case("filters_shader_carries_blend_mode", func() -> void:
		t.ok(Filters.layer_shader("add").contains("render_mode blend_add;"), "相加 = blend_add")
		t.ok(Filters.layer_shader("mul").contains("render_mode blend_mul;"), "正片叠底 = blend_mul")
		t.ok(Filters.layer_shader("mix").contains("render_mode blend_mix;"), "默认 = blend_mix")
		t.ok(Filters.blur_shader().contains("blur_step"), "模糊着色器有步长 uniform")
	)
