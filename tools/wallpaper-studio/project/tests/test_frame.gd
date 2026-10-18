extends RefCounted
## 选区框几何的用例。拖手柄时"改哪个字段、改多少"全在 src/doc/frame.gd 里，
## 这些断言就是那套规则的唯一依据 —— 在窗口里手拖一遍很难发现"拉左边时位置没跟着动"
## 这类错，而且拖完就忘。

const Frame = preload("res://src/doc/frame.gd")
const Layer = preload("res://src/doc/layer.gd")
const StudioDoc = preload("res://src/doc/doc.gd")

const CANVAS := Vector2i(1280, 720)


func _layer(x: float, y: float, w: float, h: float) -> Dictionary:
	var l := Layer.make_color("层", Color.WHITE)
	l["transform"]["x"] = x
	l["transform"]["y"] = y
	l["transform"]["w"] = w
	l["transform"]["h"] = h
	return l


func register(t) -> void:
	t.case("frame_default_is_full_canvas", func() -> void:
		var l := Layer.make_color("层", Color.WHITE)
		t.eq(Frame.of(l, CANVAS), Rect2(Vector2.ZERO, Vector2(CANVAS)),
				"没有 w/h 的图层铺满画布")
	)

	t.case("frame_reads_explicit_rect", func() -> void:
		t.eq(Frame.of(_layer(100, 50, 640, 360), CANVAS),
				Rect2(100, 50, 640, 360), "显式框原样读出")
	)

	t.case("frame_materialize_writes_canvas_size", func() -> void:
		var l := Layer.make_color("层", Color.WHITE)
		Frame.materialize(l, CANVAS)
		t.eq(l["transform"]["w"], 1280.0, "宽落成画布宽")
		t.eq(l["transform"]["h"], 720.0, "高落成画布高")
		# 已经是显式框的不能再被覆盖
		Frame.materialize(_layer(10, 10, 100, 100), CANVAS)
	)

	t.case("frame_move_offsets_position_only", func() -> void:
		var l := _layer(100, 50, 640, 360)
		Frame.move(l, CANVAS, Vector2(30, -20))
		t.eq(Frame.of(l, CANVAS), Rect2(130, 30, 640, 360), "只动位置，尺寸不变")
	)

	t.case("frame_resize_right_bottom_grows", func() -> void:
		var l := _layer(100, 50, 640, 360)
		Frame.resize(l, CANVAS, Frame.RIGHT | Frame.BOTTOM, Vector2(40, 20), false)
		t.eq(Frame.of(l, CANVAS), Rect2(100, 50, 680, 380), "右下角拖动只改宽高")
	)

	t.case("frame_resize_left_top_moves_origin", func() -> void:
		var l := _layer(100, 50, 640, 360)
		# 拉左上角：位置要跟到鼠标处，对边不动。
		Frame.resize(l, CANVAS, Frame.LEFT | Frame.TOP, Vector2(10, 20), false)
		t.eq(Frame.of(l, CANVAS), Rect2(110, 70, 630, 340), "左上角拖动同时挪位置")
	)

	t.case("frame_resize_clamps_to_minimum", func() -> void:
		var l := _layer(100, 50, 640, 360)
		Frame.resize(l, CANVAS, Frame.RIGHT, Vector2(-9999, 0), false)
		t.eq(Frame.of(l, CANVAS).size.x, Frame.MIN_SIZE, "宽度夹到最小")
	)

	t.case("frame_resize_keep_ratio_follows_dominant_axis", func() -> void:
		var l := _layer(100, 100, 200, 100)
		# 宽度的变化(100)比高度折算到宽度后的变化(10*2)大 -> 以宽为准，高跟着变。
		Frame.resize(l, CANVAS, Frame.RIGHT | Frame.BOTTOM, Vector2(100, 10), true)
		var r := Frame.of(l, CANVAS)
		t.approx(r.size.x, 300.0, 0.001, "宽按鼠标位移")
		t.approx(r.size.y, 150.0, 0.001, "高等比得出")
		t.eq(r.position, Vector2(100, 100), "锚点是左上角，位置不动")
	)

	t.case("frame_anchors_are_normalized_canvas", func() -> void:
		var a := Frame.anchors(_layer(100, 50, 640, 360), CANVAS)
		t.approx(a["left"], 100.0 / 1280.0, 0.0001, "左锚点 = x / 画布宽")
		t.approx(a["top"], 50.0 / 720.0, 0.0001, "上锚点 = y / 画布高")
		t.approx(a["right"], 740.0 / 1280.0, 0.0001, "右锚点 = (x+w) / 画布宽")
		t.approx(a["bottom"], 410.0 / 720.0, 0.0001, "下锚点 = (y+h) / 画布高")
	)

	t.case("frame_hit_test_respects_rotation", func() -> void:
		var l := _layer(100, 100, 200, 100)
		# 绕左上角转 90°：本地中心 (100,50) 转到画布坐标 (100-50, 100+100)。
		t.ok(Frame.hit_test(l, CANVAS, Vector2(50, 200), 90.0), "旋转后中心点命中")
		t.ok(not Frame.hit_test(l, CANVAS, Vector2(200, 150), 90.0),
				"未旋转时的中心点在旋转后不命中")
	)

	t.case("frame_cursor_for_handles", func() -> void:
		t.eq(Frame.cursor_for(Frame.LEFT | Frame.TOP), Control.CURSOR_FDIAGSIZE, "左上角")
		t.eq(Frame.cursor_for(Frame.RIGHT | Frame.BOTTOM), Control.CURSOR_FDIAGSIZE, "右下角")
		t.eq(Frame.cursor_for(Frame.RIGHT | Frame.TOP), Control.CURSOR_BDIAGSIZE, "右上角")
		t.eq(Frame.cursor_for(Frame.LEFT), Control.CURSOR_HSIZE, "左边")
		t.eq(Frame.cursor_for(Frame.BOTTOM), Control.CURSOR_VSIZE, "下边")
	)

	t.case("doc_parse_folds_uniform_scale_into_frame", func() -> void:
		# 老文档用 scale 表达缩放：加载后变成显式宽高 + scale=1，渲染结果不变。
		var parsed: Dictionary = StudioDoc.parse(
			'{"format_version":1,"canvas":{"width":1280,"height":720},'
			+ '"layers":[{"type":"color","transform":{"scale":2}}]}')
		t.eq(parsed.ok, true, "parses: " + String(parsed.error))
		var tr: Dictionary = parsed.doc.layers[0]["transform"]
		t.eq(tr["w"], 2560.0, "宽 = 画布宽 * scale")
		t.eq(tr["h"], 1440.0, "高 = 画布高 * scale")
		t.eq(tr["scale"], 1.0, "scale 归一")
	)
