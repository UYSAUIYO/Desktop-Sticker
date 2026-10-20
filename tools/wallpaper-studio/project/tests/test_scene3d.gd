extends RefCounted
## 3D 场景模式（mode = "3d"）的用例：文档模型、相机路径、校验、场景构建与导出。

const StudioDoc = preload("res://src/doc/doc.gd")
const Scene3D = preload("res://src/doc/scene3d.gd")
const SceneBuilder3D = preload("res://src/build/scene_builder_3d.gd")
const Exporter = preload("res://src/export/exporter.gd")
const StudioPaths = preload("res://src/doc/paths.gd")


func _tmp_project() -> String:
	var dir := "user://test_proj3d"
	DirAccess.make_dir_recursive_absolute(dir)
	return dir


func _tmp_poster() -> String:
	var p := "user://test_poster3d.png"
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 1))
	img.save_png(p)
	return p


## 返回值刻意不写类型：doc 是 RefCounted 实例，写成 -> Dictionary 会在运行时
## 被类型检查打成 null（实测整条用例就这样静默烂掉）。
func _tmp_3d_doc():
	var doc = StudioDoc.create_empty("三维壁纸", Vector2i(1920, 1080))
	doc.mode = "3d"
	doc.loop_seconds = 4.0
	doc.objects3d = Scene3D.starter_scene()
	var ring: Dictionary = doc.objects3d[2]
	ring["anim"]["spin"] = [0.0, 40.0, 0.0]
	var ground: Dictionary = doc.objects3d[0]
	ground["pos"] = [0.0, -0.75, 0.0]
	doc.camera3d["pos"] = [0.0, 1.6, 5.0]
	doc.camera3d["target"] = [0.0, 0.2, 0.0]
	doc.camera3d["fov"] = 55.0
	return doc


func register(t) -> void:
	t.case("scene3d_roundtrips_through_json", func() -> void:
		var doc = _tmp_3d_doc()
		doc.camera3d["keys"] = [
			{"t": 0.0, "pos": [0.0, 1.6, 5.0], "target": [0.0, 0.2, 0.0], "fov": 55.0},
			{"t": 4.0, "pos": [3.0, 2.2, 4.0], "target": [0.0, 0.0, 0.0], "fov": 62.0},
		]
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "3D 文档能读回来：" + String(parsed.error))
		if not parsed.ok:
			return
		var back = parsed.doc
		t.eq(back.is_3d(), true, "mode = 3d")
		t.eq(back.objects3d.size(), 4, "物体数量一致")
		t.eq(String(back.objects3d[2]["type"]), "torus", "物体类型一致")
		t.approx(float(back.objects3d[2]["anim"]["spin"][1]), 40.0, 0.001, "自转速度一致")
		t.approx(float((back.objects3d[0]["pos"] as Array)[1]), -0.75, 0.001, "地面高度一致")
		t.eq((back.camera3d["keys"] as Array).size(), 2, "相机关键帧数量一致")
		t.eq(String(back.camera3d["play"]), "loop", "播放模式默认 loop")
		t.eq(String(back.env3d["sky_top"]), String(doc.env3d["sky_top"]), "天空色一致")
		# 颜色必须是 #rrggbb 字符串，不是 Color 的 str()（和 2D 同一个坑）。
		t.ok(String(back.env3d["sky_top"]).begins_with("#"), "天空色是 #rrggbb")
	)

	t.case("scene3d_v2_document_stays_2d", func() -> void:
		# 老文档没有 mode 字段：不能因为版本号升到 3 就把老工程当成 3D。
		var doc = StudioDoc.create_empty("平面", Vector2i(640, 360))
		var parsed: Dictionary = StudioDoc.parse(doc.to_text())
		t.eq(parsed.ok, true, "能读回来")
		if parsed.ok:
			t.eq(parsed.doc.is_3d(), false, "没有 mode 字段 = 2D")
			t.eq(parsed.doc.objects3d.size(), 0, "没有 3D 物体")
	)

	t.case("scene3d_camera_path_sampling", func() -> void:
		# 关键帧的 t 是**秒**（2s 的循环，两端各一帧）。
		var cam := Scene3D.normalize_camera({
			"pos": [0.0, 0.0, 5.0], "target": [0.0, 0.0, 0.0], "fov": 50.0,
			"play": "loop",
			"keys": [
				{"t": 0.0, "pos": [0.0, 0.0, 5.0], "target": [0.0, 0.0, 0.0], "fov": 50.0},
				{"t": 2.0, "pos": [2.0, 0.0, 5.0], "target": [0.0, 0.0, 0.0], "fov": 70.0},
			],
		})
		var a = Scene3D.camera_at(cam, 0.0, 2.0)
		t.approx((a["pos"] as Vector3).x, 0.0, 0.0001, "路径起点")
		# loop 模式在 t = loop 那一刻回绕回起点（fposmod），这是播放器真实的接缝行为。
		var b = Scene3D.camera_at(cam, 2.0, 2.0)
		t.approx((b["pos"] as Vector3).x, 0.0, 0.0001, "loop 在 t=loop 回绕到起点")
		var wrap = Scene3D.camera_at(cam, 2.5, 2.0)
		t.approx((wrap["pos"] as Vector3).x, 0.5, 0.001, "loop 模式回绕（2.5s = 0.5s）")
		var mid = Scene3D.camera_at(cam, 1.0, 2.0)
		t.approx((mid["pos"] as Vector3).x, 1.0, 0.001, "线性段中点")
		t.approx(float(mid["fov"]), 60.0, 0.001, "fov 同步插值")
		# mirror：后半程往回走（3s = 折返走 1s）。
		cam["play"] = "mirror"
		var back = Scene3D.camera_at(cam, 3.0, 2.0)
		t.approx((back["pos"] as Vector3).x, 1.0, 0.001, "mirror 折返")
		# once：播完停在末尾。
		cam["play"] = "once"
		var end = Scene3D.camera_at(cam, 5.0, 2.0)
		t.approx((end["pos"] as Vector3).x, 2.0, 0.001, "once 停在终点")
		# ease：in 缓入（前半程走得慢）。
		cam["play"] = "loop"
		cam["keys"][1]["ease"] = "in"
		var eased = Scene3D.camera_at(cam, 1.0, 2.0)
		t.approx(float((eased["pos"] as Vector3).x), 0.25, 0.001, "in 缓入段中点只走了 12.5%")
	)

	t.case("scene3d_camera_keys_editing", func() -> void:
		var doc = _tmp_3d_doc()
		var cam: Dictionary = doc.camera3d
		cam["pos"] = [1.0, 2.0, 3.0]
		Scene3D.set_camera_key(cam, 0.0)
		t.eq((cam["keys"] as Array).size(), 1, "写了一个关键帧")
		# 同一时刻再写一次 = 覆盖。
		cam["pos"] = [4.0, 5.0, 6.0]
		Scene3D.set_camera_key(cam, 0.0)
		t.eq((cam["keys"] as Array).size(), 1, "同刻覆盖")
		t.approx(float((cam["keys"][0]["pos"] as Array)[0]), 4.0, 0.001, "覆盖后的值")
		Scene3D.set_camera_key(cam, 2.0)
		t.eq((cam["keys"] as Array).size(), 2, "第二个关键帧")
		t.approx(doc.key_end(), 2.0, 0.0001, "key_end = 末帧时刻")
		Scene3D.remove_camera_key(cam, 2.0)
		t.eq((cam["keys"] as Array).size(), 1, "删除关键帧")
	)

	t.case("scene3d_validates_scene", func() -> void:
		var doc = _tmp_3d_doc()
		t.eq(doc.validate("user://nowhere").size(), 0, "完整场景没有问题")
		# 空场景 = 导出一片空。
		var empty = StudioDoc.create_empty("空", Vector2i(640, 360))
		empty.mode = "3d"
		var problems: PackedStringArray = empty.validate("user://nowhere")
		t.ok(problems.size() >= 1, "空场景被拦下")
		# 模型文件不存在。
		var m = StudioDoc.create_empty("坏模型", Vector2i(640, 360))
		m.mode = "3d"
		m.objects3d = [Scene3D.make_model("家伙", "assets/missing.glb")]
		t.ok(m.validate("user://test_proj3d").size() >= 1, "缺失的模型文件被报出来")
		# 相机关键帧超出时长。
		var over = _tmp_3d_doc()
		over.loop_seconds = 1.0
		Scene3D.set_camera_key(over.camera3d, 3.0)
		t.eq(over.out_of_range_keys().size(), 1, "超出时长的相机关键帧有提示")
		t.ok(over.validate("user://nowhere").size() >= 1, "导出校验也拦")
	)

	t.case("scene3d_builder_builds_expected_tree", func() -> void:
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			return
		var doc = _tmp_3d_doc()
		var root: Node3D = SceneBuilder3D.build(doc, "user://nowhere", false)
		t.ok(root != null, "根节点是 Node3D")
		if root == null:
			return
		# AnimationPlayer 要在树里才会把轨道值落到节点上（不在树里 seek 是空转）。
		tree.root.add_child(root)
		t.ok(root.get_node_or_null("Env") is WorldEnvironment, "有环境节点")
		var cam := root.get_node_or_null("Camera") as Camera3D
		t.ok(cam != null, "有相机")
		if cam != null:
			t.approx(cam.fov, 55.0, 0.001, "fov 落到节点上")
			t.approx(cam.position.z, 5.0, 0.001, "机位落到节点上")
		var meshes := 0
		var lights := 0
		for c in root.get_children():
			if c is MeshInstance3D:
				meshes += 1
			elif c is Light3D:
				lights += 1
		t.eq(meshes, 3, "地面 + 主体 + 环 = 三个网格")
		t.eq(lights, 1, "starter 带一盏平行光")
		# 环的自转被采样进动画：0s 与 2s 的 rotation 不一样。
		var player := root.get_node_or_null("Player") as AnimationPlayer
		t.ok(player != null, "有 AnimationPlayer")
		if player != null:
			t.eq(player.current_animation, "", "autoplay=false 时没有自动播放")
			var anim := player.get_animation("wallpaper")
			t.ok(anim != null, "动画叫 wallpaper")
			if anim != null:
				t.approx(anim.length, 4.0, 0.0001, "长度 = loop_seconds")
				# 相机 3 条轨道（position / rotation / fov）+ 环 2 条 = 5。
				t.ok(anim.get_track_count() >= 5, "相机 + 自转轨道都在（%d 条）"
						% anim.get_track_count())
				var ring := root.get_node_or_null(
						NodePath("O_" + String(doc.objects3d[2]["id"]))) as Node3D
				t.ok(ring != null, "找到环节点")
				if ring != null:
					var y0 := ring.rotation_degrees.y
					SceneBuilder3D.set_player_time(player, 2.0)
					var y2 := ring.rotation_degrees.y
					t.ok(absf(y2 - y0) > 30.0, "播放到 2s 时环真的转了（%.0f -> %.0f 度）"
							% [y0, y2])
					SceneBuilder3D.set_player_time(player, 0.0)
		tree.root.remove_child(root)
		root.free()
	)

	t.case("scene3d_export_produces_3d_manifest", func() -> void:
		var doc = _tmp_3d_doc()
		var out := "user://test_3d.dswall"
		var result: Dictionary = Exporter.export(doc, _tmp_project(), out, _tmp_poster())
		t.eq(result.ok, true, "导出成功：" + String(result.error))
		if not result.ok:
			return
		var zip := ZIPReader.new()
		t.eq(zip.open(out), OK, ".dswall 是个 zip")
		var m: Dictionary = JSON.parse_string(
				zip.read_file("manifest.json").get_string_from_utf8())
		t.eq(m.dimension, "3d", "manifest 标记为 3d")
		zip.close()
		# 导出目录里的 main.scn 应该是 Node3D 场景：直接实例化验证。
		var build_abs := ProjectSettings.globalize_path(StudioPaths.build_dir(doc.pack_id))
		var packed := load(build_abs.path_join("main.scn")) as PackedScene
		t.ok(packed != null, "3D 场景可以打包")
		if packed != null:
			var inst = packed.instantiate()
			t.ok(inst is Node3D, "包里的根是 Node3D")
			t.ok(inst.get_node_or_null("Camera") is Camera3D, "包里有相机")
			var player := inst.get_node_or_null("Player") as AnimationPlayer
			t.ok(player != null, "包里有 AnimationPlayer")
			if player != null:
				t.eq(player.autoplay, "wallpaper", "导出后自动播放")
			inst.free()
	)
