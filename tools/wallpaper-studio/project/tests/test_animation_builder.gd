extends RefCounted
## 文档 -> AnimationPlayer 的用例。
##
## 三条容易坏的约定，都用断言钉住：
##   1. 位置动的是**锚点比例**，不是绝对像素（换分辨率时动图不能错位）；
##   2. 预览构建（autoplay=false）与导出构建（autoplay=true）的节点结构一致；
##   3. 没有关键帧时不挂空播放器。

const StudioDoc = preload("res://src/doc/doc.gd")
const SceneBuilder = preload("res://src/build/scene_builder.gd")
const AnimationBuilder = preload("res://src/build/animation_builder.gd")
const Layer = preload("res://src/doc/layer.gd")
const Timeline = preload("res://src/doc/timeline.gd")


func _track_of(anim: Animation, layer_id: String, prop: String) -> int:
	# 路径可能是 "L_child:prop"，也可能是 "L_group/group/L_child:prop"（容器里的子层），
	# 所以只按结尾匹配，不假设它在根上。
	var suffix := "L_" + layer_id + ":" + prop
	for i in anim.get_track_count():
		var path := String(anim.track_get_path(i))
		if path.ends_with(suffix):
			return i
	return -1


func _moving_text() -> Dictionary:
	var l := Layer.make_text("标题", "hi")
	l["transform"]["x"] = 100.0
	l["transform"]["y"] = 50.0
	l["transform"]["w"] = 200.0
	l["transform"]["h"] = 100.0
	# x/y 关键帧存父级比例（设计画布 1280 宽）。
	Timeline.set_key(l, "x", 0.0, 100.0 / 1280.0)
	Timeline.set_key(l, "x", 2.0, 300.0 / 1280.0)
	Timeline.set_key(l, "opacity", 0.0, 0.0)
	Timeline.set_key(l, "opacity", 2.0, 1.0)
	return l


func register(t) -> void:
	t.case("animation_compiles_tracks_into_player", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := _moving_text()
		doc.add_layer(layer)
		var root = SceneBuilder.build(doc, "", false)

		var player := root.get_node_or_null("Timeline") as AnimationPlayer
		t.ok(player != null, "有 key 时挂 AnimationPlayer")
		if player != null:
			t.eq(player.autoplay, "", "预览构建不自动播")
			var anim := player.get_animation("wallpaper")
			t.ok(anim != null, "动画名为 wallpaper")
			if anim != null:
				t.approx(anim.length, 8.0, 0.0001, "长度 = 文档 loop_seconds")
				t.eq(anim.loop_mode, Animation.LOOP_LINEAR, "循环播放")

				var id := String(layer["id"])
				var left := _track_of(anim, id, "anchor_left")
				var right := _track_of(anim, id, "anchor_right")
				t.ok(left >= 0 and right >= 0, "x 关键帧展开成左右两条锚点轨道")
				if left >= 0:
					t.eq(anim.track_get_key_count(left), 2, "两个关键帧")
					t.approx(float(anim.track_get_key_value(left, 0)), 100.0 / 1280.0,
							0.0001, "起点是比例，不是像素")
					t.approx(float(anim.track_get_key_value(left, 1)), 300.0 / 1280.0,
							0.0001, "终点是比例")
				if right >= 0:
					t.approx(float(anim.track_get_key_value(right, 1)), 500.0 / 1280.0,
							0.0001, "右边界 = x + w")

				var alpha := _track_of(anim, id, "modulate:a")
				t.ok(alpha >= 0, "不透明度轨道")
				if alpha >= 0:
					t.approx(float(anim.track_get_key_value(alpha, 1)), 1.0, 0.0001, "末帧不透明")
		root.free()
	)

	t.case("animation_export_build_autoplays", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(_moving_text())
		var root = SceneBuilder.build(doc, "")
		var player := root.get_node_or_null("Timeline") as AnimationPlayer
		t.ok(player != null, "导出构建同样有播放器")
		if player != null:
			t.eq(player.autoplay, "wallpaper", "进树就播")
		root.free()
	)

	t.case("animation_without_keys_adds_no_player", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		doc.add_layer(Layer.make_text("静态", "hi"))
		var root = SceneBuilder.build(doc, "")
		t.eq(root.get_node_or_null("Timeline"), null, "没有关键帧就不挂播放器")
		root.free()
	)

	t.case("animation_scale_and_rotation_use_node_properties", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := Layer.make_color("块", Color.WHITE)
		layer["transform"]["w"] = 400.0
		layer["transform"]["h"] = 200.0
		Timeline.set_key(layer, "scale", 0.0, 1.0)
		Timeline.set_key(layer, "scale", 1.0, 2.0)
		Timeline.set_key(layer, "rotation", 0.0, 0.0)
		Timeline.set_key(layer, "rotation", 1.0, 90.0)
		doc.add_layer(layer)
		var root = SceneBuilder.build(doc, "", false)
		var player := root.get_node_or_null("Timeline") as AnimationPlayer
		t.ok(player != null, "有播放器")
		if player != null:
			var anim := player.get_animation("wallpaper")
			var id := String(layer["id"])
			t.ok(_track_of(anim, id, "scale:x") >= 0, "缩放走 scale:x")
			t.ok(_track_of(anim, id, "scale:y") >= 0, "缩放走 scale:y")
			t.ok(_track_of(anim, id, "rotation_degrees") >= 0, "旋转走 rotation_degrees")
		root.free()
	)

	t.case("animation_nested_layer_path_resolves", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var group := Layer.make_group("容器", 400.0, 200.0)
		var child := Layer.make_text("子", "hi")
		child["transform"]["w"] = 100.0
		child["transform"]["h"] = 50.0
		Timeline.set_key(child, "x", 0.0, 0.0)
		Timeline.set_key(child, "x", 1.0, 100.0 / 400.0)
		group["children"] = [child]
		doc.add_layer(group)
		var root = SceneBuilder.build(doc, "", false)
		var player := root.get_node_or_null("Timeline") as AnimationPlayer
		t.ok(player != null, "容器里的子层也能动")
		if player != null:
			var anim := player.get_animation("wallpaper")
			t.ok(_track_of(anim, String(child["id"]), "anchor_left") >= 0,
					"轨道路径带上容器的层级")
			# 路径必须真的能解析到节点，否则播放器会静默不动。
			var node := root.get_node_or_null(NodePath(
					String(anim.track_get_path(_track_of(anim, String(child["id"]), "anchor_left")))
					.get_slice(":", 0)))
			t.ok(node != null, "轨道路径能解析到节点")
		root.free()
	)

	t.case("animation_player_drives_the_node", func() -> void:
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := _moving_text()
		doc.add_layer(layer)
		var root = SceneBuilder.build(doc, "", false)
		var player := root.get_node_or_null("Timeline") as AnimationPlayer
		if player == null:
			t.ok(false, "缺播放器")
			root.free()
			return

		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			root.free()
			return
		tree.root.add_child(root)
		player.play("wallpaper")
		player.advance(1.0)
		var node := root.find_child("L_" + String(layer["id"]), true, false) as Control
		t.ok(node != null, "找到节点")
		if node != null:
			t.approx(node.anchor_left, 200.0 / 1280.0, 0.0005, "播到 1 秒 = 中点")
			t.approx(node.anchor_right, 400.0 / 1280.0, 0.0005, "右边界跟着走")
			t.approx(node.modulate.a, 0.5, 0.0005, "不透明度也动")
		tree.root.remove_child(root)
		root.free()
	)

	t.case("animation_preview_seeks_the_player", func() -> void:
		# 预览不再自己采样曲线：拖播放头 = 让同一个播放器 seek。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := _moving_text()
		doc.add_layer(layer)
		var root = SceneBuilder.build(doc, "", false)
		var player := AnimationBuilder.player_of(root)
		t.ok(player != null, "预览树里挂了播放器")
		if player == null:
			root.free()
			return
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			root.free()
			return
		tree.root.add_child(root)

		AnimationBuilder.set_player_time(player, 1.0)
		var node := root.find_child("L_" + String(layer["id"]), true, false) as Control
		t.ok(node != null, "找到节点")
		if node != null:
			t.approx(node.anchor_left, 200.0 / 1280.0, 0.0005, "1 秒 = 中点")
			t.approx(node.modulate.a, 0.5, 0.0005, "不透明度同一时刻")
			t.approx(node.scale.x, 1.0, 0.0005, "没 key 的通道保持基础值")
		# 反复 seek（拖播放头）不能把播放器搞乱。
		AnimationBuilder.set_player_time(player, 0.0)
		if node != null:
			t.approx(node.anchor_left, 100.0 / 1280.0, 0.0005, "拖回 0 秒")
		tree.root.remove_child(root)
		root.free()
	)

	t.case("animation_bakes_eased_segments", func() -> void:
		# Animation 的线性轨道没有逐段缓动，所以非线性段在编译时按 60Hz 烘焙。
		# 导出的曲线必须和文档里的 ease 一致（以前预览有缓动、导出是匀速）。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := Layer.make_color("块", Color.WHITE)
		layer["transform"]["w"] = 200.0
		layer["transform"]["h"] = 100.0
		Timeline.set_key(layer, "opacity", 0.0, 0.0, "linear")
		Timeline.set_key(layer, "opacity", 2.0, 1.0, "in")
		doc.add_layer(layer)
		var root = SceneBuilder.build(doc, "", false)
		var player := AnimationBuilder.player_of(root)
		t.ok(player != null, "有播放器")
		if player == null:
			root.free()
			return
		var anim := player.get_animation("wallpaper")
		var idx := _track_of(anim, String(layer["id"]), "modulate:a")
		t.ok(idx >= 0, "不透明度轨道")
		# 2 秒 @ 60Hz 的缓动段 -> 约 120 个采样点，而不是 2 个端点。
		t.ok(anim.track_get_key_count(idx) > 30, "缓动段被烘焙成密集关键帧")

		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			root.free()
			return
		tree.root.add_child(root)
		AnimationBuilder.set_player_time(player, 1.0)
		var node := root.find_child("L_" + String(layer["id"]), true, false) as Control
		if node != null:
			# ease "in" = u^3，u=0.5 -> 0.125。
			t.approx(node.modulate.a, 0.125, 0.004, "烘焙后的曲线和文档一致")
		tree.root.remove_child(root)
		root.free()
	)

	t.case("animation_baked_values_clamp_opacity", func() -> void:
		var keys := [
			{"t": 0.0, "v": -0.5, "ease": "linear"},
			{"t": 1.0, "v": 1.5, "ease": "linear"},
		]
		var out := AnimationBuilder.baked_keys(keys, "opacity")
		t.eq(out.size(), 2, "线性段只保留端点")
		t.approx(float(out[0]["v"]), 0.0, 0.0001, "下界夹到 0")
		t.approx(float(out[1]["v"]), 1.0, 0.0001, "上界夹到 1")
		var xs := AnimationBuilder.baked_keys(keys, "x")
		t.approx(float(xs[0]["v"]), -0.5, 0.0001, "位置通道不夹")
	)

	t.case("animation_recompile_picks_up_key_edits", func() -> void:
		# 拖动画布自动写 key 之后**不能**重建节点树（手感会顿），
		# 所以曲线要能就地换进同一个播放器。
		var doc = StudioDoc.create_empty("t", Vector2i(1280, 720))
		var layer := _moving_text()
		doc.add_layer(layer)
		var root = SceneBuilder.build(doc, "", false)
		var player := AnimationBuilder.player_of(root)
		t.ok(player != null, "有播放器")
		if player == null:
			root.free()
			return
		var tree := Engine.get_main_loop() as SceneTree
		if tree == null:
			t.ok(false, "no SceneTree")
			root.free()
			return
		tree.root.add_child(root)
		AnimationBuilder.set_player_time(player, 1.0)
		var node := root.find_child("L_" + String(layer["id"]), true, false) as Control
		if node != null:
			t.approx(node.anchor_left, 200.0 / 1280.0, 0.0005, "改之前是曲线中点")

		# 在播放头写一个新 key（自动关键帧做的事），再就地重编译。
		# 注意：add_layer 会 normalize 成**副本**，所以要改文档里的那一份。
		var live: Dictionary = doc.layers[0]
		Timeline.set_key(live, "x", 1.0, 0.8)
		t.eq(AnimationBuilder.recompile(doc, root, player), true, "重编译成功")
		AnimationBuilder.set_player_time(player, 1.0)
		if node != null:
			t.approx(node.anchor_left, 0.8, 0.0005, "新 key 立刻生效")

		tree.root.remove_child(root)
		root.free()
	)
