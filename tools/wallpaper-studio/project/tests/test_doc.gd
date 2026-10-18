extends RefCounted
## 文档模型的用例。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")


func register(t) -> void:
	t.case("doc_roundtrip_preserves_layers", func() -> void:
		var doc = StudioDoc.create_empty("测试", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_color("背景", Color(0.02, 0.07, 0.12)))
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "parse ok: " + String(parsed.error))
		t.eq(parsed.doc.name, "测试", "name kept")
		t.eq(parsed.doc.canvas, Vector2i(1920, 1080), "canvas kept")
		t.eq(parsed.doc.layers.size(), 1, "one layer")
		t.eq(parsed.doc.layers[0].type, "color", "layer type kept")
		t.eq(parsed.doc.layers[0].color, Color(0.02, 0.07, 0.12), "color kept")
		t.eq(parsed.doc.pack_id, doc.pack_id, "pack id kept")
	)

	t.case("doc_missing_fields_take_defaults", func() -> void:
		var parsed: Dictionary = StudioDoc.parse('{"format_version":1,"layers":[{"type":"color"}]}')
		t.eq(parsed.ok, true, "minimal doc parses: " + String(parsed.error))
		t.eq(parsed.doc.canvas, Vector2i(1920, 1080), "default canvas")
		t.eq(parsed.doc.layers[0].name, "图层", "default name")
		t.eq(parsed.doc.layers[0].transform.x, 0.0, "default transform")
		t.eq(parsed.doc.layers[0].transform.opacity, 1.0, "default opacity")
		t.eq(parsed.doc.layers[0].visible, true, "default visible")
		t.eq(parsed.doc.layers[0].blend, "mix", "default blend")
	)

	t.case("doc_unknown_fields_are_ignored", func() -> void:
		# 前向兼容：未来版本加的字段不能让今天的加载器失败。
		var parsed: Dictionary = StudioDoc.parse(
			'{"format_version":1,"future_stuff":{"a":1},"layers":[{"type":"color","new_field":7}]}')
		t.eq(parsed.ok, true, "unknown top-level field tolerated")
		t.eq(parsed.doc.layers.size(), 1, "layer kept")
	)

	t.case("doc_rejects_future_format_version", func() -> void:
		var parsed: Dictionary = StudioDoc.parse('{"format_version":99}')
		t.eq(parsed.ok, false, "future version rejected")
		t.ok(String(parsed.error).contains("99"), "error names the version")
	)

	t.case("doc_rejects_broken_json", func() -> void:
		t.eq(StudioDoc.parse("{not json").ok, false, "broken json rejected")
		t.eq(StudioDoc.parse("[1,2,3]").ok, false, "array rejected")
	)

	t.case("doc_rejects_tiny_canvas", func() -> void:
		var parsed: Dictionary = StudioDoc.parse('{"format_version":1,"canvas":{"width":8,"height":8}}')
		t.eq(parsed.ok, false, "tiny canvas rejected")
	)

	t.case("doc_layer_order_is_bottom_first", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		doc.add_layer(Layer.make_color("上", Color.WHITE))
		# 约定：文档 index 0 = 最底层（先渲染）。UI 列表要倒序显示。
		t.eq(doc.layers[0].name, "底", "index 0 is the bottom layer")
		doc.move_layer(0, 1)
		t.eq(doc.layers[0].name, "上", "move_layer swaps")
		doc.remove_layer(0)
		t.eq(doc.layers.size(), 1, "remove_layer drops one")
	)

	t.case("doc_normalizes_bad_layer_values", func() -> void:
		var l := Layer.normalize({"type": "nonsense", "name": "", "blend": "wat",
								  "transform": {"x": "abc", "scale": 2}})
		t.eq(l.type, "color", "unknown type falls back to color")
		t.eq(l.blend, "mix", "unknown blend falls back to mix")
		t.eq(l.transform.x, 0.0, "non-numeric transform falls back")
		t.eq(l.transform.scale, 2.0, "provided transform kept")
	)

	t.case("doc_external_scripts_lists_only_user_scripts", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var l := Layer.make_image("花瓣", "assets/petals.gif")
		l["script_path"] = "assets/myscript.gd"
		doc.add_layer(l)
		t.eq(doc.external_scripts(), ["assets/myscript.gd"], "user script listed")
	)

	t.case("doc_validate_reports_missing_asset", func() -> void:
		var doc = StudioDoc.create_empty("坏壁纸", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_image("花瓣", "definitely_missing.png"))
		var problems = doc.validate("user://nowhere")
		t.eq(problems.size(), 1, "one problem")
		t.ok(String(problems[0]).contains("definitely_missing.png"), "names the missing file")
	)

	t.case("doc_validate_flags_empty_document", func() -> void:
		var doc = StudioDoc.create_empty("空的", Vector2i(1920, 1080))
		var problems = doc.validate("user://nowhere")
		t.eq(problems.size(), 1, "only the no-visible-layers problem")
		t.ok(String(problems[0]).contains("可见图层"), "mentions visible layers")
	)

	# ---------------------------------------------------------------- 树形结构

	t.case("doc_walk_reports_tree_paths", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("背景", Color.BLACK))
		var group := Layer.make_group("容器", 400.0, 200.0)
		group["children"] = [Layer.make_color("子", Color.WHITE)]
		doc.add_layer(group)
		var rows: Array = doc.walk()
		t.eq(rows.size(), 3, "背景 + 容器 + 子层")
		t.eq(rows[0]["path"], PackedInt32Array([0]), "第一个是根层 0")
		t.eq(rows[1]["path"], PackedInt32Array([1]), "第二个是容器")
		t.eq(rows[2]["path"], PackedInt32Array([1, 0]), "第三个是容器里的子层")
		t.eq(rows[2]["depth"], 1, "子层深度 1")
		t.eq(String(doc.layer_at(PackedInt32Array([1, 0]))["name"]), "子", "按路径取到子层")
	)

	t.case("doc_insert_into_group_and_remove", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var group := Layer.make_group("容器", 400.0, 200.0)
		doc.add_layer(group)
		var path: PackedInt32Array = doc.insert_layer(PackedInt32Array([0]),
				Layer.make_color("子", Color.WHITE))
		t.eq(path, PackedInt32Array([0, 0]), "插入到容器里，返回子路径")
		t.eq((doc.layers[0]["children"] as Array).size(), 1, "容器有一个子层")
		doc.remove_at(path)
		t.eq((doc.layers[0]["children"] as Array).size(), 0, "子层被删掉")
		doc.remove_at(PackedInt32Array([0]))
		t.eq(doc.layers.size(), 0, "容器被删掉")
	)

	t.case("doc_move_within_siblings", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		doc.add_layer(Layer.make_color("上", Color.WHITE))
		var moved: PackedInt32Array = doc.move_within(PackedInt32Array([0]), 1)
		t.eq(moved, PackedInt32Array([1]), "移动后路径跟着变")
		t.eq(String(doc.layers[1]["name"]), "底", "底图上移到最上层")
	)

	t.case("doc_reparent_keeps_screen_position", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("块", Color.WHITE)
		l["transform"]["x"] = 100.0
		l["transform"]["y"] = 50.0
		l["transform"]["w"] = 200.0
		l["transform"]["h"] = 100.0
		doc.add_layer(l)
		var group := Layer.make_group("容器", 400.0, 200.0)
		group["transform"]["x"] = 400.0
		group["transform"]["y"] = 300.0
		doc.add_layer(group)
		var path: PackedInt32Array = doc.reparent(PackedInt32Array([0]),
				PackedInt32Array([1]))
		var child: Dictionary = doc.layer_at(path)
		t.approx(float(child["transform"]["x"]), -300.0, 0.001, "相对容器的新 X")
		t.approx(float(child["transform"]["y"]), -250.0, 0.001, "相对容器的新 Y")
		t.approx(float(child["transform"]["w"]), 200.0, 0.001, "尺寸不变")
	)

	t.case("doc_wrap_in_group_keeps_frame", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("块", Color.WHITE)
		l["transform"]["x"] = 100.0
		l["transform"]["y"] = 50.0
		l["transform"]["w"] = 200.0
		l["transform"]["h"] = 100.0
		doc.add_layer(l)
		var gpath: PackedInt32Array = doc.wrap_in_group(PackedInt32Array([0]))
		t.eq(gpath, PackedInt32Array([0]), "容器占据原位置")
		var group: Dictionary = doc.layer_at(gpath)
		t.eq(String(group["type"]), "group", "新建的是容器")
		t.eq(group["transform"]["x"], 100.0, "容器位置 = 原图层位置")
		t.eq(group["transform"]["w"], 200.0, "容器宽 = 原图层宽")
		var child: Dictionary = doc.layer_at(PackedInt32Array([0, 0]))
		t.eq(child["transform"]["x"], 0.0, "子层在容器里从 0 开始")
		t.eq(child["transform"]["w"], 200.0, "子层铺满容器")
	)

	t.case("doc_set_canvas_scales_layers", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_color("块", Color.WHITE)
		l["transform"]["x"] = 100.0
		l["transform"]["y"] = 50.0
		l["transform"]["w"] = 200.0
		l["transform"]["h"] = 100.0
		doc.add_layer(l)
		doc.set_canvas(Vector2i(2560, 1440), true)
		var t2: Dictionary = doc.layers[0]["transform"]
		t.eq(doc.canvas, Vector2i(2560, 1440), "画布换到 2K")
		t.approx(float(t2["x"]), 200.0, 0.001, "位置按比例放大")
		t.approx(float(t2["w"]), 400.0, 0.001, "宽按比例放大")
	)

	t.case("doc_group_resize_scales_children", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var group := Layer.make_group("容器", 400.0, 200.0)
		var child := Layer.make_color("子", Color.WHITE)
		child["transform"]["w"] = 200.0
		child["transform"]["h"] = 100.0
		group["children"] = [child]
		doc.add_layer(group)
		doc.set_canvas(Vector2i(1280, 1440), true)   # 只放大高度
		var kid: Dictionary = (doc.layers[0]["children"] as Array)[0]
		t.approx(float(kid["transform"]["w"]), 200.0, 0.001, "宽度比例 = 1，子层宽不变")
		t.approx(float(kid["transform"]["h"]), 200.0, 0.001, "高度比例 = 2，子层高翻倍")
	)

	t.case("doc_validate_flags_missing_mask_image", func() -> void:
		var doc = StudioDoc.create_empty("遮罩", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var m := Layer.make_mask("遮罩层")
		m["shape"] = "image"
		m["src"] = "gone.png"
		doc.add_layer(m)
		var problems = doc.validate("user://nowhere")
		var found := false
		for p in problems:
			if String(p).contains("gone.png"):
				found = true
		t.ok(found, "缺遮罩图要报出来")
	)

	t.case("doc_validate_counts_effect_layers_as_no_content", func() -> void:
		var doc = StudioDoc.create_empty("只有遮罩", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_mask("遮罩层"))
		var problems = doc.validate("user://nowhere")
		t.eq(problems.size(), 1, "只有一条问题")
		t.ok(String(problems[0]).contains("可见图层"), "效果层不算可见内容")
	)

	# ---------------------------------------------------------------- 遮罩 / 滤镜（效果图层）

	t.case("doc_legacy_mask_and_filters_become_effect_layers", func() -> void:
		# 老文档把遮罩/滤镜挂在图层自己身上；加载时拆成紧跟其上的效果图层，
		# 覆盖层数 = 1，画出来的效果不变。
		var text := '{"format_version":1,"canvas":{"width":1280,"height":720},"layers":['
		text += '{"type":"color","name":"底","transform":{"x":100,"y":50,"w":640,"h":360},'
		text += '"mask":{"enabled":true,"shape":"rounded","w":320,"h":180,"radius":30},'
		text += '"filters":{"color":{"enabled":true,"brightness":0.25}}}]}'
		var parsed: Dictionary = StudioDoc.parse(text)
		t.eq(parsed.ok, true, "老文档能打开: " + String(parsed.error))
		var doc = parsed.doc
		t.eq(doc.layers.size(), 3, "底图 + 遮罩层 + 滤镜层")
		t.eq(String(doc.layers[1]["type"]), "mask", "遮罩变成独立图层")
		t.eq(String(doc.layers[2]["type"]), "filter", "滤镜变成独立图层")
		t.eq(Layer.cover_count(doc.layers[1]), 1, "遮罩只覆盖下面那一层")
		t.eq(float(doc.layers[1]["transform"]["x"]), 100.0, "遮罩框 = 图层位置 + 遮罩偏移")
		t.eq(float(doc.layers[1]["transform"]["w"]), 320.0, "遮罩宽保留")
		t.eq(String(doc.layers[1]["shape"]), "rounded", "形状保留")
		t.approx(float(doc.layers[2]["filters"]["color"]["brightness"]), 0.25, 0.0001,
				"滤镜参数保留")
		t.eq(doc.layers[0].has("mask"), false, "图层自己不再带 mask 字段")
		t.eq(doc.layers[0].has("filters"), false, "图层自己不再带 filters 字段")
	)

	t.case("doc_effect_layer_roundtrip", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var m := Layer.make_mask("遮罩")
		m["shape"] = "ellipse"
		m["cover"] = {"mode": "count", "count": 2}
		doc.add_layer(m)
		var fl := Layer.make_filter("滤镜")
		fl["filters"]["blur"]["enabled"] = true
		fl["filters"]["blur"]["radius"] = 9.0
		doc.add_layer(fl)
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "读回: " + String(parsed.error))
		var back = parsed.doc
		t.eq(back.layers.size(), 3, "三层都在")
		t.eq(String(back.layers[1]["shape"]), "ellipse", "遮罩形状")
		t.eq(Layer.cover_count(back.layers[1]), 2, "覆盖层数")
		t.approx(float(back.layers[2]["filters"]["blur"]["radius"]), 9.0, 0.0001,
				"模糊半径")
	)

	t.case("doc_cover_span_all_and_count", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		for i in 3:
			doc.add_layer(Layer.make_color("层%d" % i, Color.BLACK))
		var m := Layer.make_mask("遮罩")
		doc.add_layer(m)
		t.eq(doc.covered_count(PackedInt32Array([3])), 3, "覆盖下方全部 3 层")
		doc.layers[3]["cover"] = {"mode": "count", "count": 2}
		t.eq(doc.covered_count(PackedInt32Array([3])), 2, "只覆盖紧邻的 2 层")
		t.eq(doc.cover_span(PackedInt32Array([3])), Vector2i(1, 3), "区间是 [1, 3)")
		doc.layers[3]["cover"] = {"mode": "count", "count": 99}
		t.eq(doc.covered_count(PackedInt32Array([3])), 3, "层数超过兄弟数就退化成全部")
	)

	t.case("doc_cover_span_skips_hidden_effect_layers", func() -> void:
		# 隐藏的效果层不生效，也就不占"被覆盖层数"的名额（和场景构造一致）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("底", Color.BLACK))
		var hidden := Layer.make_mask("隐藏遮罩")
		hidden["visible"] = false
		doc.add_layer(hidden)
		doc.add_layer(Layer.make_color("上", Color.WHITE))
		var fl := Layer.make_filter("滤镜")
		fl["cover"] = {"mode": "count", "count": 1}
		doc.add_layer(fl)
		t.eq(doc.covered_count(PackedInt32Array([3])), 1, "只覆盖上那一层")
		t.eq(doc.cover_span(PackedInt32Array([3])), Vector2i(2, 3), "跳过隐藏的遮罩层")
	)

