extends RefCounted
## 导出器的用例。
##
## 第三个用例（exporter_produces_loadable_pack）会挂载导出的 PCK。引擎是进程级单例、
## ProjectSettings.load_resource_pack 挂载后无法卸载 —— 所以它在 DTest.EXCLUSIVE_CASES
## 里，跑全量时被跳过，由 run_tests.ps1 用 --only 单跑一个进程。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")
const Timeline = preload("res://src/doc/timeline.gd")
const StudioPaths = preload("res://src/doc/paths.gd")
const Exporter = preload("res://src/export/exporter.gd")


func _tmp_project() -> String:
	var dir := "user://test_proj"
	DirAccess.make_dir_recursive_absolute(dir)
	# 一张 4x4 的纯色 PNG 当图层资源。
	var img := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 0, 0))
	img.save_png(dir.path_join("red.png"))
	# 动图的第二帧（帧序列要至少两帧才看得出在动）。
	var img2 := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	img2.fill(Color(0, 0, 1))
	img2.save_png(dir.path_join("blue.png"))
	return dir


func _tmp_poster() -> String:
	var p := "user://test_poster.png"
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 1))
	img.save_png(p)
	return p


## 从 .dswall 里取出 project.pck 写成文件。只为测试存在。
func _pck_from_dswall(dswall: String, out_pck: String) -> bool:
	var zip := ZIPReader.new()
	if zip.open(dswall) != OK:
		return false
	var bytes := zip.read_file("project.pck")
	zip.close()
	if bytes.is_empty():
		return false
	var f := FileAccess.open(out_pck, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true


func register(t) -> void:
	t.case("exporter_reports_validation_problems", func() -> void:
		var doc = StudioDoc.create_empty("坏壁纸", Vector2i(1920, 1080))
		doc.add_layer(Layer.make_image("花瓣", "definitely_missing.png"))
		var result: Dictionary = Exporter.export(doc, "user://nowhere", "user://out.dswall", "")
		t.eq(result.ok, false, "export blocked")
		t.eq(result.problem_list.size(), 1, "one problem reported")
		t.ok(not FileAccess.file_exists("user://out.dswall"), "no file written on validation failure")
	)

	t.case("exporter_uses_binary_scene_to_avoid_text_bloat", func() -> void:
		# 实测逼出来的约束：运行库不认识裸 .png，贴图只能内联；而文本 .tscn 会把内联字节
		# 写成十进制数字列表（~4x 膨胀）。所以场景必须是二进制 .scn。
		# 这里用一张 256x256 的噪声图把膨胀差异放大到可断言的程度。
		var proj := _tmp_project()
		var noise := Image.create(256, 256, false, Image.FORMAT_RGBA8)
		for y in 256:
			for x in 256:
				noise.set_pixel(x, y, Color(float(x) / 255.0, float(y) / 255.0, float((x * y) % 255) / 255.0))
		noise.save_png(proj.path_join("noise.png"))
		var png_size := FileAccess.get_file_as_bytes(proj.path_join("noise.png")).size()

		var doc = StudioDoc.create_empty("膨胀检查", Vector2i(320, 240))
		doc.add_layer(Layer.make_image("噪声", "noise.png"))
		var result: Dictionary = Exporter.export(doc, proj, "user://ref.dswall", "")
		t.eq(result.ok, true, "export ok: " + String(result.error))

		var build_abs := ProjectSettings.globalize_path(StudioPaths.build_dir(doc.pack_id))
		t.eq(FileAccess.file_exists(build_abs.path_join("main.scn")), true, "scene is binary .scn")
		t.eq(FileAccess.file_exists(build_abs.path_join("main.tscn")), false, "no text scene")
		var scn_size := FileAccess.get_file_as_bytes(build_abs.path_join("main.scn")).size()
		# 场景里不该有像素：256x256 RGBA 内联是 256 KB，文本格式还要再乘 ~4。
		# 断言远小于原始像素体积，才算真的走了"运行时解码"这条路。
		t.ok(scn_size < png_size, "scene holds no pixel data (%d bytes, png is %d)"
				% [scn_size, png_size])

		# 但贴图必须真的能用：直接实例化这张场景，看 TextureRect 有没有解码出纹理。
		var built := load(ProjectSettings.globalize_path(build_abs.path_join("main.scn"))) as PackedScene
		t.ok(built != null, "built scene loadable")
		var node := built.instantiate()
		var found := false
		# 内容节点现在嵌在 layer box / stack 里，按类型递归找，别只看直接子节点。
		for c in node.find_children("*", "TextureRect", true, false):
			found = true
			t.eq(c.texture, null, "no pixels inlined in the scene")
			t.ok(String(c.get("src")).begins_with("res://build/"), "script points at the packed asset")
			# _ready 只在节点进树时才跑；这里直接调它，等价于运行时那一刻做的事。
			c.call("_apply")
			t.ok(c.texture != null, "runtime image script decoded the texture")
			t.eq(c.texture.get_width(), 256, "decoded texture width")
			t.eq(c.texture.get_height(), 256, "decoded texture height")
		node.free()
		t.ok(found, "found the image layer")
	)

	t.case("exporter_produces_loadable_pack", func() -> void:
		var proj := _tmp_project()
		var doc = StudioDoc.create_empty("导出台", Vector2i(320, 240))
		doc.loop_seconds = 4.0
		doc.add_layer(Layer.make_color("底", Color(0, 0, 0)))
		doc.add_layer(Layer.make_image("红块", "red.png"))
		var clock := Layer.make_text("时钟", "00:00:00")
		clock["dynamic"] = "clock"
		doc.add_layer(clock)

		# 阶段 2 的两条新链路一起验：关键帧 -> AnimationPlayer，帧序列 -> 运行时装动图。
		var petal := Layer.make_image("花瓣", "")
		petal["frames"] = ["red.png", "blue.png"]
		petal["fps"] = 6.0
		Timeline.set_key(petal, "y", 0.0, 0.0)
		Timeline.set_key(petal, "y", 2.0, 120.0 / 240.0)   # y 存父级比例（画布高 240）
		# 缓动段：包里必须是烘焙过的密集关键帧（否则导出就成了匀速）。
		Timeline.set_key(petal, "opacity", 0.0, 0.0, "linear")
		Timeline.set_key(petal, "opacity", 2.0, 1.0, "in")
		doc.add_layer(petal)

		var out := "user://test_out.dswall"
		var result: Dictionary = Exporter.export(doc, proj, out, _tmp_poster())
		t.eq(result.ok, true, "export ok: " + String(result.error))

		var zip := ZIPReader.new()
		t.eq(zip.open(out), OK, "dswall opens as a zip")
		var entries := zip.get_files()
		t.ok("project.pck" in entries, "has project.pck")
		t.ok("manifest.json" in entries, "has manifest.json")
		t.ok("poster.png" in entries, "has poster.png")

		var m: Dictionary = JSON.parse_string(zip.read_file("manifest.json").get_string_from_utf8())
		t.eq(m.dimension, "2d", "dimension is 2d")
		t.eq(int(m.format_version), 1, "format version is 1")
		t.eq(m.pack_id, doc.pack_id, "pack id preserved")
		t.eq(String(m.main_scene).begins_with("res://build/"), true, "main scene lives under build/")
		t.eq(m.content_flags, [], "no user scripts, so no scripts flag")
		t.eq(m.requested_permissions, [], "nothing declared for a plain image wallpaper")
		zip.close()

		# 加密包能被本进程挂载。（这一步污染进程，所以本用例独占一个 runner。）
		var pck := "user://test_out.pck"
		t.eq(_pck_from_dswall(out, pck), true, "extracted project.pck")
		t.eq(ProjectSettings.load_resource_pack(ProjectSettings.globalize_path(pck), false), true,
				"encrypted pack mounts (key must match the one baked into libgodot)")
		var main_scene := String(m.main_scene)
		t.ok(ResourceLoader.exists(main_scene), "main scene loadable from the pack")

		# 时钟脚本在包里仍然活着 —— 预览与导出共用同一份源码，这是它的最终验证。
		var scene := load(main_scene) as PackedScene
		t.ok(scene != null, "main scene is a PackedScene")
		var inst := scene.instantiate()
		# 注意：图片层现在也带脚本（运行时解码），所以不能"第一个有脚本的节点"就算时钟。
		# 内容节点嵌在 layer box / stack 里，递归找。
		var found_clock := false
		var found_image_script := false
		var found_animated := false
		var player: AnimationPlayer = null
		for c in inst.find_children("*", "", true, false):
			if c is AnimationPlayer:
				player = c as AnimationPlayer
			if c.get_script() == null:
				continue
			if "mode" in c:
				found_clock = true
				t.eq(c.get("mode"), "clock", "clock mode survived packing")
			elif "frames" in c and (c.get("frames") as PackedStringArray).size() > 0:
				found_animated = true
				t.eq((c.get("frames") as PackedStringArray).size(), 2, "两帧都进了包")
				t.approx(float(c.get("fps")), 6.0, 0.0001, "帧率写进场景")
				c.call("_apply_frames")
				t.ok(c.texture is AnimatedTexture, "运行库把帧序列解成 AnimatedTexture")
				if c.texture is AnimatedTexture:
					t.eq((c.texture as AnimatedTexture).frames, 2, "动图帧数")
			elif "src" in c:
				found_image_script = true
		t.eq(found_clock, true, "the injected clock script survived packing")
		t.eq(found_image_script, true, "the injected image script survived packing")
		t.eq(found_animated, true, "the injected animated-image script survived packing")

		# 关键帧进包：AnimationPlayer 存在、动画存在、autoplay 设好。
		t.ok(player != null, "AnimationPlayer survived packing")
		if player != null:
			t.eq(player.autoplay, "wallpaper", "autoplay survives packing")
			var anim := player.get_animation("wallpaper")
			t.ok(anim != null, "animation survived packing")
			if anim != null:
				t.approx(anim.length, 4.0, 0.0001, "length = loop_seconds")
				t.ok(anim.get_track_count() >= 2, "y 关键帧展开成上下两条锚点轨道")
				var alpha := -1
				for i in anim.get_track_count():
					if String(anim.track_get_path(i)).ends_with(":modulate:a"):
						alpha = i
				t.ok(alpha >= 0, "包里有不透明度轨道")
				if alpha >= 0:
					t.ok(anim.track_get_key_count(alpha) > 30,
							"缓动段在包里仍是烘焙过的（不是线性端点）")
		inst.free()
	)
