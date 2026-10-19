extends PanelContainer
## 时间轴面板（底部停靠）：播放/暂停、时长、播放头，以及选中图层的五条通道。
##
## 左侧只放通道名和"＋"（在播放头加关键帧），真正的轨道区是自绘的 TimelineTrack。
## 两边行高必须完全一致，否则「＋」和关键帧会错行 —— 行高常量只在 TimelineTrack 里定义。
##
## 面板不自己存播放头：唯一真源是 CanvasView.time（画布要按它渲染），这里只是视图。

const Timeline = preload("res://src/doc/timeline.gd")
const TrackView = preload("res://src/ui/timeline_track.gd")

## 行高必须和轨道区完全一致，否则「＋」和关键帧会错行 —— 直接用它的常量。
const LEFT_W := 132.0

var main = null

var _btn_play: Button
var _btn_start: Button
var _time_label: Label
var _length: SpinBox
var _left: VBoxContainer
var _body: HBoxContainer
var _btn_collapse: Button
var _btn_fit: Button
var _btn_close: Button
var _status: Label
var _track = null
var _collapsed := false


func setup(m) -> void:
	main = m
	# 面板 _ready 时 main 还是 null（装配顺序：先 add_child 再 setup），
	# 所以轨道区的 main 必须在这里补上，否则它永远画不出关键帧。
	if _track != null:
		_track.setup(m)


func _ready() -> void:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.095, 0.1, 0.112)
	sb.border_width_top = 1
	sb.border_color = Color(0.2, 0.21, 0.245)
	sb.content_margin_left = 8
	sb.content_margin_right = 8
	sb.content_margin_top = 2
	sb.content_margin_bottom = 2
	add_theme_stylebox_override("panel", sb)
	custom_minimum_size = Vector2(0, 30 + TrackView.RULER_H + TrackView.ROW_H * 5 + 8)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 0)
	add_child(box)

	var header := HBoxContainer.new()
	header.custom_minimum_size = Vector2(0, 28)
	header.add_theme_constant_override("separation", 6)
	box.add_child(header)

	var title := Label.new()
	title.text = "时间轴"
	title.tooltip_text = "双击轨道加关键帧 · 拖动改时间 · 右键删除 · 点标尺拖播放头"
	title.add_theme_font_size_override("font_size", 12)
	title.modulate = Color(0.78, 0.81, 0.87)
	header.add_child(title)

	_btn_collapse = _flat_button(header, "收起", "收起 / 展开时间轴")
	_btn_collapse.pressed.connect(_toggle_collapse)

	_btn_play = _flat_button(header, "播放", "播放 / 暂停（空格）")
	_btn_play.toggle_mode = false
	_btn_play.pressed.connect(func() -> void:
		if main != null:
			main.toggle_play()
			sync_play_button()
	)

	_btn_start = _flat_button(header, "◀◀", "回到开头")
	_btn_start.pressed.connect(func() -> void:
		if main != null:
			main.seek_time(0.0)
	)

	_time_label = Label.new()
	_time_label.text = "0.00 / 8.00 s"
	_time_label.custom_minimum_size = Vector2(96, 0)
	_time_label.add_theme_font_size_override("font_size", 11)
	_time_label.modulate = Color(0.68, 0.72, 0.79)
	header.add_child(_time_label)

	var sp := Control.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(sp)

	var len_label := Label.new()
	len_label.text = "时长"
	len_label.add_theme_font_size_override("font_size", 11)
	len_label.modulate = Color(0.68, 0.72, 0.79)
	header.add_child(len_label)

	_length = SpinBox.new()
	_length.min_value = 0.25
	_length.max_value = 600.0
	_length.step = 0.25
	_length.value = 8.0
	_length.custom_minimum_size = Vector2(82, 0)
	_length.tooltip_text = "循环时长（秒）。关键帧可以超出它，但播放到末端就回到 0。"
	_length.value_changed.connect(_on_length_changed)
	header.add_child(_length)

	# 循环时长比末帧短 / 首尾接不上时，这里给一句明确的话和一个一键修复。
	_btn_fit = _flat_button(header, "适配末帧", "把循环时长拉到最后一个关键帧")
	_btn_fit.visible = false
	_btn_fit.pressed.connect(_on_fit_length)

	_btn_close = _flat_button(header, "闭合循环", "在循环终点补一帧（值 = 0 时刻），回绕不跳")
	_btn_close.visible = false
	_btn_close.pressed.connect(_on_close_loop)

	_status = Label.new()
	_status.add_theme_font_size_override("font_size", 11)
	_status.modulate = Color(0.95, 0.62, 0.35)
	# 窄窗口时宁可截断，也不把「时长」和修复按钮挤出去。
	_status.clip_text = true
	_status.custom_minimum_size = Vector2(0, 0)
	header.add_child(_status)

	_body = HBoxContainer.new()
	_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_theme_constant_override("separation", 0)
	box.add_child(_body)

	_left = VBoxContainer.new()
	_left.custom_minimum_size = Vector2(LEFT_W, 0)
	_left.add_theme_constant_override("separation", 0)
	_body.add_child(_left)

	_track = TrackView.new()
	_track.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_track.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_track.custom_minimum_size = Vector2(0, TrackView.RULER_H + TrackView.ROW_H * 5)
	_body.add_child(_track)
	_track.setup(main)


## 收起时只留标题行：不用时间轴的人不该被它占掉 180px 画布高度。
func _toggle_collapse() -> void:
	_collapsed = not _collapsed
	_body.visible = not _collapsed
	custom_minimum_size = Vector2(0, 30 if _collapsed else 30 + TrackView.RULER_H
			+ TrackView.ROW_H * 5 + 8)
	_btn_collapse.text = "展开" if _collapsed else "收起"


func _flat_button(parent: Node, text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.flat = true
	b.tooltip_text = tip
	b.add_theme_font_size_override("font_size", 12)
	parent.add_child(b)
	return b


func _process(_delta: float) -> void:
	if main == null or main._canvas == null:
		return
	if main._canvas.playing:
		sync_time()


## 播放状态变化 / 播放头移动后刷新显示（不重建控件）。
func sync_time() -> void:
	if main == null or main._canvas == null or main.doc == null:
		return
	if _time_label != null:
		_time_label.text = "%.2f / %.2f s" % [main._canvas.time, main.doc.timeline_end()]
	if _track != null:
		_track.queue_redraw()
	sync_play_button()


func sync_play_button() -> void:
	if _btn_play == null or main == null or main._canvas == null:
		return
	var text := "暂停" if main._canvas.playing else "播放"
	if _btn_play.text != text:
		_btn_play.text = text


## 选中变化 / 文档重建后调用：重建左侧通道行。
func refresh() -> void:
	if main == null or main.doc == null:
		return
	if _length != null:
		_length.set_value_no_signal(main.doc.loop_seconds)
	_clear_left()
	var layer: Dictionary = main.selected_layer()
	if layer.is_empty():
		_left.add_child(_spacer())
		var empty := Label.new()
		empty.text = "（未选中图层）"
		empty.add_theme_font_size_override("font_size", 11)
		empty.modulate = Color(0.5, 0.54, 0.6)
		_left.add_child(empty)
	else:
		_left.add_child(_spacer())
		var effect := String(layer.get("type", "")) in ["mask", "filter"]
		if effect:
			# 效果层的几何是着色器 uniform，不是节点属性：这里不给通道行，避免
			# "加了 key 但画面不动"的假象。
			var hint := Label.new()
			hint.text = "效果层暂不支持关键帧"
			hint.add_theme_font_size_override("font_size", 10)
			hint.modulate = Color(0.55, 0.5, 0.42)
			_left.add_child(hint)
		else:
			for channel in Timeline.CHANNELS:
				_left.add_child(_channel_row(String(channel)))
	if _track != null:
		_track.queue_redraw()
	refresh_warnings()
	sync_time()


func _spacer() -> Control:
	var c := Control.new()
	c.custom_minimum_size = Vector2(0, TrackView.RULER_H)
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return c


func _channel_row(channel: String) -> Control:
	var row := HBoxContainer.new()
	row.custom_minimum_size = Vector2(0, TrackView.ROW_H)
	row.add_theme_constant_override("separation", 2)

	var lab := Label.new()
	lab.text = String(Timeline.CHANNEL_NAMES.get(channel, channel))
	lab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lab.add_theme_font_size_override("font_size", 11)
	lab.modulate = Color(0.62, 0.66, 0.73)
	row.add_child(lab)

	var add := Button.new()
	add.text = "＋"
	add.flat = true
	add.custom_minimum_size = Vector2(20, 20)
	add.add_theme_font_size_override("font_size", 12)
	add.tooltip_text = "在播放头给「%s」加一个关键帧" % String(Timeline.CHANNEL_NAMES.get(channel, channel))
	add.pressed.connect(func() -> void: _add_key(channel))
	row.add_child(add)
	return row


func _clear_left() -> void:
	for c in _left.get_children():
		_left.remove_child(c)
		c.queue_free()


## 在播放头加关键帧。值取**当前采样值**，所以加帧不会让画面跳一下。
func _add_key(channel: String) -> void:
	if main == null or main.doc == null:
		return
	var layer: Dictionary = main.selected_layer()
	if layer.is_empty() or String(layer.get("type", "")) in ["mask", "filter"]:
		return
	var t: float = main._canvas.time if main._canvas != null else 0.0
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	var value: float = Timeline.value_at(layer, channel, t, parent_size)
	Timeline.set_key(layer, channel, t, value, "linear")
	main.mark_dirty()
	main.apply_change(false)
	main.seek_time(t)


func _on_length_changed(v: float) -> void:
	if main == null or main.doc == null:
		return
	main.doc.loop_seconds = v
	main.mark_dirty()
	main.apply_change(false)
	refresh_warnings()
	sync_time()


## 时长比末帧短 / 首尾不接时的提示与一键修复。
func refresh_warnings() -> void:
	if _status == null or main == null or main.doc == null:
		return
	var over: Array = main.doc.out_of_range_keys()
	var total := 0
	for o in over:
		total += int(o["count"])
	var seam: Dictionary = main.doc.loop_seam()
	_btn_fit.visible = total > 0
	_btn_close.visible = total == 0 and not bool(seam["ok"])
	if total > 0:
		_status.text = "%d 个关键帧超出时长，不会播放" % total
		_status.modulate = Color(0.95, 0.62, 0.35)
	elif not bool(seam["ok"]):
		_status.text = "循环接缝会跳：%s" % String(seam["where"])
		_status.modulate = Color(0.95, 0.72, 0.4)
	else:
		_status.text = ""


func _on_fit_length() -> void:
	if main == null or main.doc == null:
		return
	main.doc.loop_seconds = clampf(main.doc.key_end(), 0.25, 600.0)
	_length.set_value_no_signal(main.doc.loop_seconds)
	main.mark_dirty()
	main.apply_change(false)
	refresh_warnings()


func _on_close_loop() -> void:
	if main == null or main.doc == null:
		return
	if main.doc.close_loop() <= 0:
		return
	main.mark_dirty()
	main.apply_change(false)
	refresh_warnings()
	sync_time()
