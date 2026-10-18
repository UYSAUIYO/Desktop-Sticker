extends RefCounted
## 文档模型的用例。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const Filters = preload("res://src/doc/filters.gd")


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

	t.case("doc_cover_range_picks_middle_layers", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("A", Color.RED))
		doc.add_layer(Layer.make_color("B", Color.GREEN))
		doc.add_layer(Layer.make_color("C", Color.BLUE))
		var m := Layer.make_mask("遮罩")
		m["cover"] = {"mode": "range", "from": 2, "to": 3}
		doc.add_layer(m)                      # 下标 3，位于 C 上面
		var path := PackedInt32Array([3])
		t.eq(doc.cover_capacity(path), 3, "下方可以覆盖 3 层")
		t.eq(doc.covered_count(path), 2, "第 2~3 层共 2 层")
		t.eq(doc.cover_span(path), Vector2i(0, 2), "真的是 A、B 两层（[0, 2)）")
		t.eq(doc.covered_names(path), ["B", "A"], "名字自上而下 = B、A")
		# 只盖中间一层：位置 2 = B。
		doc.layers[3]["cover"] = {"mode": "range", "from": 2, "to": 2}
		t.eq(doc.cover_span(path), Vector2i(1, 2), "只盖 B")
		# 起点超出下方层数 = 什么都不盖（不是退化成"全部"）。
		doc.layers[3]["cover"] = {"mode": "range", "from": 4, "to": 6}
		t.eq(doc.covered_count(path), 0, "起点超出就不覆盖")
		# 终点越界按层数截断。
		doc.layers[3]["cover"] = {"mode": "range", "from": 2, "to": 99}
		t.eq(doc.covered_count(path), 2, "终点越界截断到最底层")
	)

	t.case("doc_cover_range_survives_roundtrip", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("A", Color.RED))
		doc.add_layer(Layer.make_color("B", Color.GREEN))
		var fl := Layer.make_filter("滤镜")
		fl["filters"]["blur"]["enabled"] = true
		fl["cover"] = {"mode": "range", "from": 1, "to": 1}
		doc.add_layer(fl)
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "读回: " + String(parsed.error))
		t.eq(String(parsed.doc.layers[2]["cover"]["mode"]), "range", "模式保留")
		t.eq(int(parsed.doc.layers[2]["cover"]["from"]), 1, "起点保留")
		t.eq(parsed.doc.covered_count(PackedInt32Array([2])), 1, "覆盖 1 层")
	)

	# ---------------------------------------------------------------- 绑定到指定图层

	t.case("doc_cover_bind_follows_target_when_layers_move", func() -> void:
		# 绑定模式的意义就是"只挂在这一层上"：目标上下移动、中间插进别的图层，
		# 覆盖的始终是它 —— 这正是"别整屏套滤镜"最稳的用法。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_color("A", Color.RED))
		var b := Layer.make_color("B", Color.GREEN)
		doc.add_layer(b)
		var fl := Layer.make_filter("只作用于 B")
		fl["cover"] = {"mode": "layer", "target": String(b["id"])}
		doc.add_layer(fl)                        # 下标 2，紧挨着 B 上方
		var path := PackedInt32Array([2])
		t.eq(Layer.cover_target(doc.layers[2]), String(b["id"]), "cover.target 存的是 B 的 id")
		t.eq(doc.cover_target_path(path), PackedInt32Array([1]), "目标路径 = B")
		t.eq(doc.covered_count(path), 1, "只覆盖 B 一层")
		t.eq(doc.covered_names(path), ["B"], "覆盖的是 B")

		# 在 A 和 B 之间插一层：B 变成下标 2，效果层变成下标 3，绑定仍在 B 上。
		doc.insert_layer(PackedInt32Array(), Layer.make_color("C", Color.BLUE), 1)
		t.eq(doc.covered_names(PackedInt32Array([3])), ["B"], "插入图层后仍绑 B")
		t.eq(doc.covered_count(PackedInt32Array([3])), 1, "还是只要 B 一层")
		# 把效果层挪到最下面（位置无关）——覆盖的仍然只有 B。
		doc.move_within(PackedInt32Array([3]), -3)
		t.eq(doc.covered_names(PackedInt32Array([0])), ["B"], "效果层换位置也不变")
		t.eq(doc.covered_count(PackedInt32Array([0])), 1, "仍只覆盖 1 层")
	)

	t.case("doc_cover_bind_rejects_bad_targets", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("A", Color.RED)
		doc.add_layer(a)
		var other := Layer.make_mask("别的遮罩")
		doc.add_layer(other)
		var fl := Layer.make_filter("滤镜")
		fl["cover"] = {"mode": "layer", "target": String(other["id"])}
		doc.add_layer(fl)
		# 下标 2 的滤镜绑到了下标 1 的遮罩层：效果层没有内容可包，不生效。
		t.eq(doc.cover_target_path(PackedInt32Array([2])).is_empty(), true, "不能绑到效果层")
		t.eq(doc.covered_count(PackedInt32Array([2])), 0, "覆盖 0 层")

		doc.layers[2]["cover"] = {"mode": "layer", "target": String(a["id"])}
		t.eq(doc.covered_count(PackedInt32Array([2])), 1, "改成正常目标后生效")
		# 目标被删掉：不生效、不崩。
		doc.remove_at(PackedInt32Array([0]))
		t.eq(doc.covered_count(PackedInt32Array([1])), 0, "目标删除后覆盖 0 层")
		t.eq(doc.covered_names(PackedInt32Array([1])).size(), 0, "没有覆盖名字")
	)

	t.case("doc_cover_bind_is_same_parent_only", func() -> void:
		# 效果层的坐标空间是它所在父级的，跨容器绑定会让遮罩框对不上 —— 所以不允许，
		# 想给容器里的元素加效果就把效果层加进那个容器。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("A", Color.RED)
		var b := Layer.make_color("B", Color.GREEN)
		var group := Layer.make_group("容器", 640.0, 360.0)
		group["children"] = [a, b]
		doc.add_layer(group)
		var outside := Layer.make_filter("外面的滤镜")
		outside["cover"] = {"mode": "layer", "target": String(a["id"])}
		doc.add_layer(outside)
		t.eq(doc.cover_target_path(PackedInt32Array([1])).is_empty(), true, "跨容器绑定被拒绝")
		t.eq(doc.covered_count(PackedInt32Array([1])), 0, "外面的滤镜不生效")

		var inside := Layer.make_filter("容器内滤镜")
		inside["cover"] = {"mode": "layer", "target": String(a["id"])}
		doc.insert_layer(PackedInt32Array([0]), inside, 2)     # 容器最上层
		t.eq(doc.covered_names(PackedInt32Array([0, 2])), ["A"], "同父级可以绑定")
		t.eq(doc.covered_count(PackedInt32Array([0, 2])), 1, "覆盖容器里的 A")
	)

	t.case("doc_cover_bind_survives_roundtrip", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var b := Layer.make_color("B", Color.GREEN)
		doc.add_layer(b)
		var fl := Layer.make_filter("滤镜")
		fl["cover"] = {"mode": "layer", "target": String(b["id"])}
		doc.add_layer(fl)
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "读回: " + String(parsed.error))
		t.eq(String(parsed.doc.layers[1]["cover"]["mode"]), "layer", "模式保留")
		t.eq(String(parsed.doc.layers[1]["cover"]["target"]), String(b["id"]), "目标 id 保留")
		t.eq(parsed.doc.covered_count(PackedInt32Array([1])), 1, "读回后仍然覆盖 B")
	)

	t.case("doc_filter_region_roundtrip", func() -> void:
		# 作用区域是滤镜层自己的持久字段：存盘/读回不能丢（丢了就等于"框白拖了"）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("A", Color.RED)
		doc.add_layer(a)
		var fl := Layer.make_filter("滤镜")
		fl["region"] = {"enabled": true, "shape": "ellipse", "radius": 12.0,
				"feather": 6.0, "invert": true}
		fl["transform"]["x"] = 40.0
		fl["transform"]["y"] = 20.0
		fl["transform"]["w"] = 200.0
		fl["transform"]["h"] = 100.0
		doc.add_layer(fl)
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "读回: " + String(parsed.error))
		var back: Dictionary = parsed.doc.layers[1]
		t.eq(Filters.region_enabled(back), true, "区域开关保留")
		t.eq(String(back["region"]["shape"]), "ellipse", "区域形状保留")
		t.approx(float(back["region"]["radius"]), 12.0, 0.0001, "圆角保留")
		t.eq(bool(back["region"]["invert"]), true, "反相保留")
		t.eq(Frame.of(back, Vector2(1280, 720)),
				Rect2(Vector2(40, 20), Vector2(200, 100)), "区域框保留")
	)

	t.case("doc_covered_bounds_uses_content_frames", func() -> void:
		# 「限制作用区域」打开时拿它当框的初始值：必须是**被覆盖内容**的包围盒，
		# 而不是整张画布（否则一打开就全屏，等于没限制）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var a := Layer.make_color("A", Color.RED)
		a["transform"]["x"] = 100.0
		a["transform"]["y"] = 50.0
		a["transform"]["w"] = 200.0
		a["transform"]["h"] = 100.0
		doc.add_layer(a)
		var b := Layer.make_color("B", Color.GREEN)
		b["transform"]["x"] = 400.0
		b["transform"]["y"] = 300.0
		b["transform"]["w"] = 100.0
		b["transform"]["h"] = 50.0
		doc.add_layer(b)
		var fl := Layer.make_filter("滤镜")
		doc.add_layer(fl)
		t.eq(doc.covered_bounds(PackedInt32Array([2])),
				Rect2(Vector2(100, 50), Vector2(400, 300)), "两层内容的包围盒")

		fl["cover"] = {"mode": "layer", "target": String(b["id"])}
		doc.layers[2]["cover"] = {"mode": "layer", "target": String(b["id"])}
		t.eq(doc.covered_bounds(PackedInt32Array([2])),
				Rect2(Vector2(400, 300), Vector2(100, 50)), "绑定模式只看目标那一层")

		# 没有可覆盖内容时退化成整个父级（= 不限制，和 Frame.of 对 w/h = 0 的语义一致）。
		var lonely = StudioDoc.create_empty("空", Vector2i(1280, 720))
		lonely.add_layer(Layer.make_filter("孤零零的滤镜"))
		t.eq(lonely.covered_bounds(PackedInt32Array([0])),
				Rect2(Vector2.ZERO, Vector2(1280, 720)), "没有可覆盖内容 = 整个父级")
	)

