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
