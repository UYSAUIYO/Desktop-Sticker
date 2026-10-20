extends RefCounted
## 时间轴纯逻辑的用例：规范化、采样、缓动、增删关键帧、文档往返。
##
## 这里的断言刻意钉在"边界"上（首帧之前、末帧之后、单帧、同一时刻写两次），
## 因为时间轴最容易出问题的地方不是插值公式，而是这些边角：
## 回绕、负时间、重复 key 会让画布上的动画看起来"偶尔跳一下"。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")
const Timeline = preload("res://src/doc/timeline.gd")
const TrackView = preload("res://src/ui/timeline_track.gd")


func register(t) -> void:
	t.case("timeline_normalize_washes_bad_tracks", func() -> void:
		var raw := [
			{"channel": "x", "keys": [{"t": 2, "v": 20}, {"t": 0, "v": 0}]},
			{"channel": "bogus", "keys": [{"t": 0, "v": 1}]},
			{"channel": "opacity", "keys": []},
			{"channel": "rotation", "keys": [{"t": -5, "v": 45, "ease": "nope"}]},
			"not a dict",
		]
		var out := Timeline.normalize_tracks(raw)
		t.eq(out.size(), 2, "只留下两条能用的 track")
		t.eq(String(out[0]["channel"]), "x", "通道顺序按输入")
		t.approx(float(out[0]["keys"][0]["t"]), 0.0, 0.0001, "关键帧按时间升序")
		t.approx(float(out[0]["keys"][1]["t"]), 2.0, 0.0001, "第二帧在后面")
		t.eq(String(out[1]["keys"][0]["ease"]), "linear", "未知缓动回落线性")
		t.approx(float(out[1]["keys"][0]["t"]), 0.0, 0.0001, "负时间被夹到 0")
	)

	t.case("timeline_normalize_merges_same_time", func() -> void:
		var out := Timeline.normalize_tracks([
			{"channel": "y", "keys": [
				{"t": 1.0, "v": 10}, {"t": 1.00001, "v": 20}, {"t": 3.0, "v": 30},
			]},
		])
		t.eq((out[0]["keys"] as Array).size(), 2, "同一时刻只留一个")
		t.approx(float(out[0]["keys"][0]["v"]), 20.0, 0.0001, "后写的覆盖前一个")
	)

	t.case("timeline_sample_track_clamps_ends", func() -> void:
		var track := {"channel": "x", "keys": [
			{"t": 1.0, "v": 10.0, "ease": "linear"},
			{"t": 3.0, "v": 50.0, "ease": "linear"},
		]}
		t.approx(float(Timeline.sample_track(track, 0.0)), 10.0, 0.0001, "首帧之前取首值")
		t.approx(float(Timeline.sample_track(track, 2.0)), 30.0, 0.0001, "区间中点线性")
		t.approx(float(Timeline.sample_track(track, 9.0)), 50.0, 0.0001, "末帧之后保持末值")
		t.eq(Timeline.sample_track({"channel": "x", "keys": []}, 1.0), null, "空轨返回 null")
	)

	t.case("timeline_sample_single_key_is_constant", func() -> void:
		var track := {"channel": "opacity", "keys": [{"t": 2.0, "v": 0.25}]}
		t.approx(float(Timeline.sample_track(track, 0.0)), 0.25, 0.0001, "首帧前")
		t.approx(float(Timeline.sample_track(track, 5.0)), 0.25, 0.0001, "首帧后")
	)

	t.case("timeline_ease_curves", func() -> void:
		t.approx(Timeline.ease_value(0.5, "linear"), 0.5, 0.0001, "线性")
		t.approx(Timeline.ease_value(0.5, "in"), 0.125, 0.0001, "缓入 = 三次方")
		t.approx(Timeline.ease_value(0.5, "out"), 0.875, 0.0001, "缓出 = 1-(1-u)^3")
		t.approx(Timeline.ease_value(0.5, "in_out"), 0.5, 0.0001, "缓入缓出中点对称")
		t.approx(Timeline.ease_value(2.0, "linear"), 1.0, 0.0001, "越界被夹住")
	)

	t.case("timeline_set_key_overwrites_same_time", func() -> void:
		var layer := Layer.make_text("标题", "hi")
		Timeline.set_key(layer, "x", 1.0, 10.0)
		Timeline.set_key(layer, "x", 1.0, 12.0)
		var track := Timeline.find_track(Timeline.tracks_of(layer), "x")
		t.eq((track["keys"] as Array).size(), 1, "同刻只留一个关键帧")
		t.approx(float(track["keys"][0]["v"]), 12.0, 0.0001, "新值覆盖旧值")

		var idx := Timeline.set_key(layer, "x", 0.0, 0.0)
		t.eq(idx, 0, "插入更早的帧后下标跟着变")
		t.eq((track["keys"] as Array).size(), 2, "多了一个关键帧")
		t.eq(Timeline.remove_key(layer, "x", 0), true, "删除成功")
		t.eq(Timeline.remove_key(layer, "x", 5), false, "越界删除返回 false")
		Timeline.remove_key(layer, "x", 0)
		t.eq(Timeline.find_track(Timeline.tracks_of(layer), "x"), {}, "通道空了整条 track 被摘掉")
		t.eq(Timeline.has_animation(layer), false, "没有轨道 = 没有动画")
	)

	t.case("timeline_sample_layer_falls_back_to_base", func() -> void:
		var layer := Layer.make_text("标题", "hi")
		layer["transform"] = {"x": 100.0, "y": 50.0, "w": 200.0, "h": 100.0,
				"rotation": 10.0, "opacity": 0.5, "scale": 1.0}
		# x/y 关键帧的单位是**父级比例**，不是像素。
		Timeline.set_key(layer, "x", 0.0, 0.0)
		Timeline.set_key(layer, "x", 2.0, 0.5)
		var pose := Timeline.sample_layer(layer, 1.0, Vector2(1280, 720))
		t.approx(float(pose["x"]), 0.25, 0.0001, "x 走关键帧（比例）")
		t.approx(float(pose["y"]), 50.0 / 720.0, 0.0001, "y 没轨道，回落基础值的比例")
		t.approx(float(pose["rotation"]), 10.0, 0.0001, "旋转回落基础值")
		t.approx(float(pose["opacity"]), 0.5, 0.0001, "不透明度回落基础值")
		t.approx(float(pose["scale"]), 1.0, 0.0001, "缩放默认 1")

		var size := Timeline.frame_size(layer, Vector2(1280, 720))
		t.eq(size, Vector2(200, 100), "显式框尺寸")
		var fill := Layer.make_color("底", Color.BLACK)
		t.eq(Timeline.frame_size(fill, Vector2(1280, 720)), Vector2(1280, 720),
				"没设尺寸 = 铺满父级")

		# 像素 <-> 比例只在界面边界换算。
		t.approx(Timeline.to_doc("x", 320.0, Vector2(1280, 720)), 0.25, 0.0001, "px -> 比例")
		t.approx(Timeline.to_px("y", 0.25, Vector2(1280, 720)), 180.0, 0.0001, "比例 -> px")
		t.approx(Timeline.to_doc("opacity", 0.4, Vector2(1280, 720)), 0.4, 0.0001,
				"无量纲通道原样返回")
	)

	t.case("timeline_resize_keeps_relative_motion", func() -> void:
		# 换画布尺寸 = 关键帧一个都不用改：比例不变，位置与运动幅度自动翻倍。
		# 这是这套单位换来的核心性质（以前存像素，换 4K 后动画只飘 1920px）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var l := Layer.make_text("标题", "hi")
		l["transform"] = {"x": 0.0, "y": 0.0, "w": 200.0, "h": 100.0,
				"rotation": 0.0, "opacity": 1.0, "scale": 1.0}
		Timeline.set_key(l, "x", 0.0, 0.25)
		Timeline.set_key(l, "x", 2.0, 0.75)
		doc.add_layer(l)
		t.approx(Timeline.rect_at(l, 0.0, Vector2(doc.canvas)).position.x, 320.0, 0.0001,
				"1280 画布上 0.25 = 320px")

		doc.set_canvas(Vector2i(2560, 1440), true)
		var moved: Dictionary = doc.layers[0]
		var track := Timeline.find_track(Timeline.tracks_of(moved), "x")
		t.approx(float(track["keys"][0]["v"]), 0.25, 0.0001, "关键帧值没有被改写")
		t.approx(Timeline.rect_at(moved, 0.0, Vector2(doc.canvas)).position.x, 640.0, 0.0001,
				"换 4K 画布后位置按比例放大")
		t.approx(Timeline.rect_at(moved, 2.0, Vector2(doc.canvas)).position.x, 1920.0, 0.0001,
				"运动幅度跟着画布放大")

		# scale_layers=false = 元素保持像素尺寸：关键帧的像素位置也要保持，
		# 比例按 旧/新 折算（这里 2560 -> 640 是 4 倍）。
		doc.set_canvas(Vector2i(640, 360), false)
		var kept: Dictionary = doc.layers[0]
		var tr2 := Timeline.find_track(Timeline.tracks_of(kept), "x")
		t.approx(float(tr2["keys"][0]["v"]), 1.0, 0.0001, "比例按旧/新画布折算")
		t.approx(Timeline.rect_at(kept, 0.0, Vector2(doc.canvas)).position.x, 640.0, 0.0001,
				"像素位置不变")
	)

	t.case("timeline_doc_roundtrip_keeps_tracks", func() -> void:
		var doc = StudioDoc.create_empty("动画", Vector2i(1280, 720))
		doc.loop_seconds = 6.5
		var layer := Layer.make_text("标题", "hi")
		Timeline.set_key(layer, "opacity", 0.0, 0.0, "in")
		Timeline.set_key(layer, "opacity", 2.0, 1.0, "out")
		doc.add_layer(layer)

		var parsed := StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "能解析回来")
		if not parsed.ok:
			return
		t.approx(float(parsed.doc.loop_seconds), 6.5, 0.0001, "loop_seconds 往返")
		var track := Timeline.find_track(Timeline.tracks_of(parsed.doc.layers[0]), "opacity")
		t.eq((track["keys"] as Array).size(), 2, "关键帧一个不少")
		t.eq(String(track["keys"][1]["ease"]), "out", "缓动方式往返")
		t.approx(float(parsed.doc.timeline_end()), 6.5, 0.0001, "时长 = loop_seconds（权威值）")

		# 改短就是真的变短：末帧仍然在文档里，但不会播放（导出会被校验拦住）。
		parsed.doc.loop_seconds = 1.0
		t.approx(float(parsed.doc.timeline_end()), 1.0, 0.0001, "改短后循环长度就是 1s")
		t.approx(float(parsed.doc.key_end()), 2.0, 0.0001, "末帧还留在文档里")

		# 循环接缝：首帧值 != 末帧值 -> 会跳。
		t.eq(bool(parsed.doc.loop_seam()["ok"]), false, "首尾值不同会被标出来")
		t.ok(parsed.doc.close_loop() > 0, "闭合循环补了关键帧")
		t.eq(bool(parsed.doc.loop_seam()["ok"]), true, "补完首尾一致")
		var closed := Timeline.find_track(
				Timeline.tracks_of(parsed.doc.layers[0]), "opacity")
		var closed_key := false
		for k in closed["keys"]:
			if absf(float(k["t"]) - 1.0) < 0.0001:
				closed_key = true
				t.approx(float(k["v"]), 0.0, 0.0001, "闭合帧取 0 时刻的值")
		t.eq(closed_key, true, "闭合帧落在循环终点")
	)

	t.case("doc_loop_length_is_authoritative", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.loop_seconds = 4.0
		var l := Layer.make_text("标题", "hi")
		Timeline.set_key(l, "opacity", 0.0, 0.0)
		Timeline.set_key(l, "opacity", 6.0, 1.0)
		doc.add_layer(l)

		t.approx(float(doc.timeline_end()), 4.0, 0.0001, "播放长度 = 时长")
		t.approx(float(doc.key_end()), 6.0, 0.0001, "末帧在 6s")
		var over: Array = doc.out_of_range_keys()
		t.eq(over.size(), 1, "有一条通道超出时长")
		if over.size() == 1:
			t.eq(int(over[0]["count"]), 1, "一个关键帧超出")
		var problems: PackedStringArray = doc.validate("")
		var flagged := false
		for p in problems:
			if String(p).contains("超出循环时长"):
				flagged = true
		t.eq(flagged, true, "导出校验会拦住超范围关键帧")

		# 适配末帧之后恢复正常。
		doc.loop_seconds = doc.key_end()
		t.eq(doc.out_of_range_keys().size(), 0, "时长拉到末帧后没有超范围")
		t.approx(float(doc.timeline_end()), 6.0, 0.0001, "循环长度跟着变成 6s")
	)

	t.case("timeline_ruler_labels_avoid_unsupported_formats", func() -> void:
		# 标尺标签以前用 "%.2g"，Godot 的 String % 不认 %g：每帧刷
		# "unsupported format character"，而且非整数秒的标签画不出来（实测 0.5s 是空的）。
		t.eq(TrackView._fmt(2.0), "2 s", "整数秒")
		t.eq(TrackView._fmt(0.25), "0.25 s", "两位小数")
		t.eq(TrackView._fmt(1.5), "1.5 s", "一位小数不带多余的 0")
	)
