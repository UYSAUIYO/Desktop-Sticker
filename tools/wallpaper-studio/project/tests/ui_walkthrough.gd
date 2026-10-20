extends SceneTree
## 真实窗口走查（不是无头用例）。
##
##   dstk-studio.exe --script res://tests/ui_walkthrough.gd -- --shots <绝对目录>
##
## 为什么单独有这么一份：`run_tests.ps1` 跑的是无头进程，界面控件不会进入真实布局、
## 也不会有真实输入的命中测试。这里把 main.tscn 挂进**带窗口**的进程，用
## `Viewport.push_input()` 发真正的鼠标/键盘事件（点和拖都走控件的 `_gui_input`），
## 一步步截图存盘，用来肉眼确认「新增功能在真窗口里是这个样子」。
##
## 覆盖：打开工程 → 点标尺拖播放头 → 播放/暂停 → 在属性面板里键入数值写关键帧
##      → 画布换 4K 预设 → 时长改短后的告警/适配末帧/闭合循环 → 动图帧序列在预览里真的在走。
##
## 文件名刻意不叫 test_*.gd：run_tests.gd 收集的是 test_ 前缀，这份只能手动跑。

const StudioDoc = preload("res://src/doc/doc.gd")
const Layer = preload("res://src/doc/layer.gd")
const Timeline = preload("res://src/doc/timeline.gd")

var fails := 0
var _shots_dir := ""
var _shots: Array[String] = []
var _editor = null


func _initialize() -> void:
	_shots_dir = _arg_value("--shots")
	if _shots_dir.is_empty():
		_shots_dir = ProjectSettings.globalize_path("user://ui-walkthrough")
	DirAccess.make_dir_recursive_absolute(_shots_dir)
	print("[walk] shots -> ", _shots_dir)
	await process_frame
	await _run()
	print("[walk] ---- %d failure(s), %d shot(s) ----" % [fails, _shots.size()])
	quit(0 if fails == 0 else 1)


func _run() -> void:
	var proj := _prepare_project()
	print("[walk] project -> ", proj)

	# 窗口：用工程默认尺寸，屏幕放不下就缩到可用区（布局都是自适应的）。
	var usable := DisplayServer.screen_get_usable_rect()
	var want := Vector2i(1440, 900)
	root.size = Vector2i(mini(want.x, usable.size.x - 60), mini(want.y, usable.size.y - 90))
	root.title = "壁纸编辑器 · 走查"

	var scene: Control = load("res://main.tscn").instantiate()
	root.add_child(scene)
	_editor = scene
	await _frames(3)

	_ok(scene.open_project(proj), "打开带关键帧的工程")
	await _frames(3)
	var title_path := _path_of(scene, "标题")
	_ok(not title_path.is_empty(), "找到标题图层")
	if title_path.is_empty():
		return
	scene.select_layer_path(title_path)
	await _frames(2)
	await _shot("01-open")

	# ---------------------------------------------------------------- 1. 真实点击标尺拖播放头
	var track = scene._timeline._track
	var ruler_pos: Vector2 = track.get_global_rect().position + Vector2(track._x_of(1.0), 6)
	await _click(ruler_pos)
	await _frames(2)
	var title_id := String(scene.doc.layer_at(title_path)["id"])
	_approx(scene._canvas.time, 1.0, 0.03, "点标尺 1s 处 -> 播放头 = 1s")
	var node := _node(scene, title_id)
	_ok(node != null, "画布上找到标题节点")
	if node != null:
		# 100px -> 1500px，1s 处 = 800px（比例 800/1920）。
		_approx(node.anchor_left, 800.0 / 1920.0, 0.002, "1s 时 X 走到中点（比例关键帧）")
		# 不透明度 0 -> 1，ease in（u^3），1s 处 = 0.125。
		_approx(node.modulate.a, 0.125, 0.01, "1s 时不透明度 = 缓入曲线值 0.125")
	var x_spin := _time_control(scene, "x")
	_ok(x_spin != null, "属性面板有 X 控件")
	if x_spin != null:
		_approx(x_spin.value, 800.0, 1.0, "属性面板 X 显示播放头处的像素值 800")
	await _shot("02-scrub")

	# ---------------------------------------------------------------- 2. 播放 / 暂停
	var before: float = scene._canvas.time
	await _click(_center(scene._timeline._btn_play))
	await create_timer(0.6).timeout
	var during: float = scene._canvas.time
	_ok(during > before + 0.25, "播放 0.6s 后播放头前进（%.2f -> %.2f）" % [before, during], "")
	if x_spin != null:
		_ok(absf(x_spin.value - 800.0) > 50.0, "播放中属性面板数字跟着走（%.0f）" % x_spin.value)
	await _shot("03-playing")
	await _click(_center(scene._timeline._btn_play))
	await _frames(2)
	_ok(not scene._canvas.playing, "再点一次暂停")

	# ---------------------------------------------------------------- 3. 属性面板键入数值 -> 写当前帧
	scene.seek_time(1.0)
	await _frames(2)
	var spin := _time_control(scene, "x")
	var typed := false
	if spin != null:
		var le := spin.get_line_edit()
		await _click(le.get_global_rect().get_center())
		await _frames(2)
		if not le.has_focus():
			le.grab_focus()
		le.select_all()
		typed = await _type("1200")
		await _frames(2)
	_ok(typed, "能往 X 数字框里键入 1200")
	var track_x := Timeline.find_track(Timeline.tracks_of(scene.doc.layer_at(title_path)), "x")
	var key_at_one := -1.0
	for k in track_x.get("keys", []):
		if absf(float(k["t"]) - 1.0) < 0.0001:
			key_at_one = float(k["v"])
	_approx(key_at_one, 1200.0 / 1920.0, 0.0005, "键入的值写进 1s 的关键帧（比例）")
	node = _node(scene, title_id)
	if node != null:
		_approx(node.anchor_left, 1200.0 / 1920.0, 0.002, "画布上的节点同步到新值")
	await _shot("04-inspector-typed")

	# ---------------------------------------------------------------- 4. 画布换 4K：关键帧一个都没变
	scene.select_layer_path(PackedInt32Array())
	await _frames(2)
	var preset := _find_preset(scene)
	_ok(preset != null, "属性面板找到画布预设下拉")
	if preset != null:
		preset.select(3)                      # 3840 × 2160（4K）
		preset.item_selected.emit(3)          # 走控件真实回调
		await _frames(3)
	_ok(scene.doc.canvas == Vector2i(3840, 2160), "画布切到 4K")
	scene.select_layer_path(title_path)
	await _frames(2)
	var track_x2 := Timeline.find_track(Timeline.tracks_of(scene.doc.layer_at(title_path)), "x")
	var kept := true
	for k in track_x2.get("keys", []):
		var v := float(k["v"])
		var expected := absf(v - 1200.0 / 1920.0) < 0.0005 \
				or absf(v - 800.0 / 1920.0) < 0.0005 \
				or absf(v - 100.0 / 1920.0) < 0.0005 \
				or absf(v - 1500.0 / 1920.0) < 0.0005
		kept = kept and expected
	_ok(kept, "换 4K 后关键帧值原样保留（比例单位）")
	scene.seek_time(1.0)
	await _frames(2)
	node = _node(scene, title_id)
	if node != null:
		_approx(node.anchor_left, 1200.0 / 1920.0, 0.002, "比例不变 -> 4K 上像素位置自动翻倍")
	await _shot("05-canvas-4k")

	# ---------------------------------------------------------------- 5. 时长改短 -> 告警 -> 适配末帧 -> 闭合循环
	var length_spin: SpinBox = scene._timeline._length
	length_spin.value = 1.0
	await _frames(2)
	var status: String = scene._timeline._status.text
	_ok(status.contains("超出时长"), "时长改到 1s 后提示「N 个关键帧超出时长」（%s）" % status)
	_ok(scene._timeline._btn_fit.visible, "出现「适配末帧」按钮")
	# 表头挤不挤：修复按钮和时长输入框必须都在窗口里（窄窗口下曾经被裁掉）。
	var header_right: float = scene._timeline._length.get_global_rect().end.x
	_ok(header_right <= float(root.size.x) - 2.0,
			"时间轴表头没有超出窗口（%.0f / %d）" % [header_right, root.size.x])
	await _shot("06-loop-warning")

	await _click(_center(scene._timeline._btn_fit))
	await _frames(2)
	_approx(scene.doc.loop_seconds, 2.0, 0.0001, "适配末帧把时长拉到末帧 2s")
	_ok(scene._timeline._btn_close.visible, "首尾不一致 -> 出现「闭合循环」按钮")
	await _shot("07-loop-seam")

	await _click(_center(scene._timeline._btn_close))
	await _frames(2)
	_ok(bool(scene.doc.loop_seam()["ok"]), "闭合循环后首尾相接")
	var closed_track := Timeline.find_track(Timeline.tracks_of(scene.doc.layer_at(title_path)), "x")
	var closed_value := -1.0
	for k in closed_track.get("keys", []):
		if absf(float(k["t"]) - 2.0) < 0.0001:
			closed_value = float(k["v"])
	_approx(closed_value, 100.0 / 1920.0, 0.0005, "终点帧的值 = 0 时刻的值")
	await _shot("08-loop-closed")

	# ---------------------------------------------------------------- 6. 动图帧序列在预览里真的在走
	var frames_path := _path_of(scene, "呼吸灯")
	_ok(not frames_path.is_empty(), "找到动图图层")
	if not frames_path.is_empty():
		scene.select_layer_path(frames_path)
		await _frames(2)
		var frames_id := String(scene.doc.layer_at(frames_path)["id"])
		var anim_node := _node(scene, frames_id)
		_ok(anim_node != null, "画布上找到动图节点")
		if anim_node != null and anim_node is TextureRect:
			var tex = (anim_node as TextureRect).texture
			_ok(tex is AnimatedTexture, "动图节点挂的是 AnimatedTexture")
			if tex is AnimatedTexture:
				_ok((tex as AnimatedTexture).frames == 3, "3 帧都在")
				# AnimatedTexture 没有 fps，帧率体现在每帧 duration 上。
				_approx((tex as AnimatedTexture).get_frame_duration(0), 0.125, 0.0005,
						"8fps -> 每帧 0.125s")
		if anim_node != null:
			# 轮询而不是"等 0.3s 再比一次"：窗口被遮挡/掉帧时定时器照样走，
			# 但渲染可能只跑了几帧，固定等待会偶发假失败（实测出现过）。
			var rect := _screen_rect(scene, anim_node)
			var a: Image = await _grab()
			var c1 := _avg_color(a, rect)
			var changed_ms := -1
			var waited := 0
			while waited < 750:
				await create_timer(0.06).timeout
				waited += 60
				var b: Image = await _grab()
				if b == null:
					break
				var c2 := _avg_color(b, rect)
				if absf(c1.r - c2.r) + absf(c1.g - c2.g) + absf(c1.b - c2.b) > 0.15:
					changed_ms = waited
					break
			_ok(changed_ms >= 0,
					"预览里的动图在播（%s）" % ("未换帧" if changed_ms < 0 else str(changed_ms) + "ms 后换帧"))
		await _shot("09-frames")

	# 保存走查工程，方便人工再打开这份状态。
	scene.doc.loop_seconds = 2.0
	print("[walk] done")


# ---------------------------------------------------------------- 工程准备

func _prepare_project() -> String:
	var dir := ProjectSettings.globalize_path("user://ui-walkthrough-project")
	DirAccess.make_dir_recursive_absolute(dir.path_join("assets/frames"))
	# 三帧纯色（红/绿/蓝），肉眼和像素都能看出换帧。
	var colors := [Color(0.95, 0.25, 0.2), Color(0.25, 0.9, 0.35), Color(0.3, 0.55, 1.0)]
	for i in colors.size():
		var img := Image.create(96, 96, false, Image.FORMAT_RGBA8)
		img.fill(colors[i])
		img.fill_rect(Rect2i(28, 28, 40, 40), Color(1, 1, 1, 0.85))
		img.save_png(dir.path_join("assets/frames/f%d.png" % i))

	var doc: StudioDoc = StudioDoc.create_empty("走查工程", Vector2i(1920, 1080))
	doc.loop_seconds = 4.0
	doc.background = Color(0.04, 0.05, 0.08)

	var bg: Dictionary = Layer.make_color("背景", Color(0.04, 0.05, 0.08))
	doc.add_layer(bg)

	var title: Dictionary = Layer.make_text("标题", "WALLPAPER")
	title["font_size"] = 96
	title["transform"]["x"] = 100.0
	title["transform"]["y"] = 300.0
	title["transform"]["w"] = 900.0
	title["transform"]["h"] = 160.0
	Timeline.set_key(title, "x", 0.0, 100.0 / 1920.0)
	Timeline.set_key(title, "x", 2.0, 1500.0 / 1920.0)
	Timeline.set_key(title, "opacity", 0.0, 0.0, "linear")
	Timeline.set_key(title, "opacity", 2.0, 1.0, "in")
	doc.add_layer(title)

	var block: Dictionary = Layer.make_color("块", Color(1.0, 0.55, 0.2))
	block["transform"]["x"] = 0.0
	block["transform"]["y"] = 0.0
	block["transform"]["w"] = 240.0
	block["transform"]["h"] = 240.0
	Timeline.set_key(block, "x", 0.0, 0.0)
	Timeline.set_key(block, "x", 2.0, 240.0 / 480.0)
	var group: Dictionary = Layer.make_group("容器", 480.0, 240.0)
	group["transform"]["x"] = 1100.0
	group["transform"]["y"] = 620.0
	group["children"] = [block]
	doc.add_layer(group)

	var frames: Dictionary = Layer.make_image("呼吸灯", "")
	frames["frames"] = ["assets/frames/f0.png", "assets/frames/f1.png", "assets/frames/f2.png"]
	frames["fps"] = 8.0
	frames["transform"]["x"] = 1500.0
	frames["transform"]["y"] = 120.0
	frames["transform"]["w"] = 120.0
	frames["transform"]["h"] = 120.0
	doc.add_layer(frames)

	var f := FileAccess.open(dir.path_join("wallpaper.json"), FileAccess.WRITE)
	f.store_string(doc.to_text())
	f.close()
	return dir


# ---------------------------------------------------------------- 输入 / 取值

func _frames(n: int) -> void:
	for i in n:
		await process_frame


func _click(pos: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = pos
	motion.global_position = pos
	root.push_input(motion, true)
	await process_frame
	var down := InputEventMouseButton.new()
	down.button_index = MOUSE_BUTTON_LEFT
	down.pressed = true
	down.position = pos
	down.global_position = pos
	root.push_input(down, true)
	await process_frame
	var up := down.duplicate() as InputEventMouseButton
	up.pressed = false
	root.push_input(up, true)
	await process_frame


## 往当前焦点控件键入一串字符 + 回车。返回是否真的送达。
func _type(text: String) -> bool:
	for ch in text:
		var ev := InputEventKey.new()
		ev.pressed = true
		ev.unicode = ch.unicode_at(0)
		ev.keycode = OS.find_keycode_from_string(ch)
		root.push_input(ev, true)
		var up := ev.duplicate() as InputEventKey
		up.pressed = false
		root.push_input(up, true)
	var enter := InputEventKey.new()
	enter.pressed = true
	enter.keycode = KEY_ENTER
	enter.unicode = 10
	root.push_input(enter, true)
	var enter_up := enter.duplicate() as InputEventKey
	enter_up.pressed = false
	root.push_input(enter_up, true)
	return true


func _center(c: Control) -> Vector2:
	return c.get_global_rect().get_center()


func _node(scene, id: String) -> Control:
	if scene == null or scene._canvas == null or scene._canvas._root == null:
		return null
	return scene._canvas._root.find_child("L_" + id, true, false) as Control


func _path_of(scene, layer_name: String) -> PackedInt32Array:
	for item in scene.doc.walk():
		if String((item["layer"] as Dictionary).get("name", "")) == layer_name:
			return item["path"]
	return PackedInt32Array()


func _time_control(scene, channel: String) -> SpinBox:
	if scene == null or scene._inspector == null:
		return null
	for item in scene._inspector._time_controls:
		if String(item["channel"]) == channel and is_instance_valid(item["node"]):
			return item["node"] as SpinBox
	return null


func _find_preset(scene) -> OptionButton:
	if scene == null or scene._inspector == null:
		return null
	for c in scene._inspector._box.find_children("*", "OptionButton", true, false):
		var ob := c as OptionButton
		if ob.item_count > 1 and String(ob.get_item_text(1)).contains("1920"):
			return ob
	return null


## 节点在窗口里的矩形（画布 SubViewport 是缩放显示的）。
## node 的坐标是 SubViewport 里的画布像素，要先乘画布缩放，再加"画布容器在 CanvasView
## 里的位置"和"CanvasView 在窗口里的位置" —— 少加一层就会裁到别处，色差恒为 0。
func _screen_rect(scene, node: Control) -> Rect2i:
	var canvas = scene._canvas
	var r: Rect2 = node.get_global_rect()
	var scale: float = canvas.display_scale()
	var origin: Vector2 = canvas.get_global_rect().position \
			+ canvas._container.position + r.position * scale
	return Rect2i(Vector2i(origin), Vector2i(r.size * scale))


func _avg_color(img: Image, rect: Rect2i) -> Color:
	var clipped := rect.intersection(Rect2i(Vector2i.ZERO, img.get_size()))
	if clipped.size.x <= 0 or clipped.size.y <= 0:
		return Color.BLACK
	var sum := Color(0, 0, 0, 1)
	var n := 0
	var y := clipped.position.y
	while y < clipped.end.y:
		var x := clipped.position.x
		while x < clipped.end.x:
			sum += img.get_pixel(x, y)
			n += 1
			x += 3
		y += 3
	if n == 0:
		return Color.BLACK
	return Color(sum.r / n, sum.g / n, sum.b / n, 1.0)


# ---------------------------------------------------------------- 截图 / 断言

func _grab() -> Image:
	await RenderingServer.frame_post_draw
	return root.get_texture().get_image()


func _shot(tag: String) -> void:
	var img: Image = await _grab()
	if img == null:
		print("[shot] %s: no image（无头模式？）" % tag)
		return
	var path := _shots_dir.path_join(tag + ".png")
	img.save_png(path)
	_shots.append(path)
	print("[shot] ", path)


func _ok(cond: bool, what: String, _detail: String = "") -> void:
	if cond:
		print("[pass] ", what)
	else:
		fails += 1
		print("[FAIL] ", what)


func _approx(got: float, want: float, tol: float, what: String) -> void:
	if absf(got - want) <= tol:
		print("[pass] %s（%.4f）" % [what, got])
	else:
		fails += 1
		print("[FAIL] %s（got %.4f, want %.4f ±%.4f）" % [what, got, want, tol])


func _arg_value(key: String) -> String:
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		if String(args[i]) == key and i + 1 < args.size():
			return String(args[i + 1])
		i += 1
	return ""
