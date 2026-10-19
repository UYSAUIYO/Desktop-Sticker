extends Control
## 时间轴的轨道区：标尺 + 每个通道一行关键帧。全部自绘 —— 用 Label/Button 拼一行
## 关键帧既重又难对齐，而且拖动时要频繁改位置，自绘最省事。
##
## 交互（和常见非编软件一致）：
##   · 点标尺           —— 拖播放头（画布实时跟随）
##   · 点关键帧         —— 选中并左右拖动改时间（0.05s 吸附，按住 Shift 不吸附）
##   · 双击空白         —— 在该通道的该时刻插入关键帧（值 = 当前时刻的采样值，不会跳）
##   · 右键关键帧       —— 删除
##   · 点通道空白       —— 只移动播放头（不误加帧）
##
## 这里只碰文档与 main.seek_time / recompile_animation，不碰节点树：
## 「拖动时不重建整棵树」这条规则和画布一致。

const Timeline = preload("res://src/doc/timeline.gd")

const PAD := 10.0
const RULER_H := 22.0
const ROW_H := 24.0
const KEY_RADIUS := 5.0
const HIT_RADIUS := 7.0
const SNAP := 0.05

const RULER_BG := Color(0.115, 0.12, 0.135)
const ROW_BG_A := Color(0.09, 0.095, 0.11)
const ROW_BG_B := Color(0.082, 0.086, 0.098)
const GRID := Color(0.165, 0.175, 0.2, 0.9)
const ROW_LINE := Color(0.16, 0.17, 0.19)
const KEY_COLOR := Color(0.95, 0.78, 0.36)
const KEY_SELECTED := Color(1.0, 0.94, 0.72)
## 超出循环时长的关键帧：画成警示色（它们不会播放，导出会被校验拦住）。
const KEY_OUT := Color(0.95, 0.42, 0.35)
const OUT_SHADE := Color(0.5, 0.14, 0.11, 0.16)
const LOOP_LINE := Color(0.95, 0.58, 0.3, 0.85)
const PLAYHEAD := Color(0.55, 0.78, 1.0)

var main = null

var _drag_channel := ""
var _drag_index := -1
var _drag_t0 := 0.0
var _selected_channel := ""
var _selected_index := -1
var _scrubbing := false


func setup(m) -> void:
	main = m
	mouse_filter = Control.MOUSE_FILTER_STOP
	focus_mode = Control.FOCUS_CLICK
	clip_contents = true


## 当前选中图层（不是内容图层时返回空字典：效果层没有可动的节点几何）。
func layer() -> Dictionary:
	if main == null or main.doc == null:
		return {}
	var l: Dictionary = main.selected_layer()
	if l.is_empty() or _is_effect(l):
		return {}
	return l


static func _is_effect(layer: Dictionary) -> bool:
	var t := String(layer.get("type", ""))
	return t == "mask" or t == "filter"


func channels() -> Array:
	return Timeline.CHANNELS if not layer().is_empty() else []


## 显示跨度：循环长度与末帧的较大者。超出循环的关键帧也要画出来（警示色），
## 否则用户改短时长后就"看不见"那些不会播放的帧了。
func length() -> float:
	if main == null or main.doc == null:
		return 1.0
	return maxf(0.25, maxf(main.doc.loop_seconds, main.doc.key_end()))


## 循环终点（播放器回绕的位置）。
func loop_length() -> float:
	if main == null or main.doc == null:
		return 1.0
	return maxf(0.25, main.doc.loop_seconds)


func _x_of(t: float) -> float:
	var span := maxf(0.0001, length())
	return PAD + clampf(t / span, 0.0, 1.0) * maxf(8.0, size.x - PAD * 2.0)


func _t_of(x: float) -> float:
	var span := maxf(0.0001, length())
	var usable := maxf(8.0, size.x - PAD * 2.0)
	return clampf((x - PAD) / usable * span, 0.0, span)


func _row_y(i: int) -> float:
	return RULER_H + i * ROW_H


# ---------------------------------------------------------------- 绘制

func _draw() -> void:
	if main == null or main.doc == null:
		return
	var w := size.x
	var h := size.y
	draw_rect(Rect2(0, 0, w, h), Color(0.075, 0.078, 0.09))

	var l := layer()
	var chans := channels()

	# 标尺
	draw_rect(Rect2(0, 0, w, RULER_H), RULER_BG)
	var font := ThemeDB.fallback_font
	var step := _tick_step()
	var t := 0.0
	while t <= length() + 0.0001:
		var x := _x_of(t)
		draw_line(Vector2(x, RULER_H - 6.0), Vector2(x, RULER_H), GRID, 1.0)
		draw_string(font, Vector2(x + 3.0, 13.0), _fmt(t), HORIZONTAL_ALIGNMENT_LEFT,
				-1, 10, Color(0.62, 0.66, 0.73))
		var half := t + step * 0.5
		if half <= length():
			var hx := _x_of(half)
			draw_line(Vector2(hx, RULER_H - 3.0), Vector2(hx, RULER_H),
					Color(0.24, 0.25, 0.28), 1.0)
		t += step

	# 通道行
	for i in chans.size():
		var y := _row_y(i)
		var r := Rect2(0, y, w, ROW_H)
		if r.position.y > h:
			break
		draw_rect(r, ROW_BG_A if i % 2 == 0 else ROW_BG_B)
		draw_line(Vector2(PAD, y + ROW_H * 0.5), Vector2(w - PAD, y + ROW_H * 0.5),
				ROW_LINE, 1.0)

	# 超出循环时长的区域：压暗 + 一条"循环终点"竖线。
	var loop_t := loop_length()
	var loop_x := _x_of(loop_t)
	if loop_x < w - PAD - 1.0:
		draw_rect(Rect2(loop_x, 0, w - loop_x, h), OUT_SHADE)
		var y := 0.0
		while y < h:
			draw_line(Vector2(loop_x, y), Vector2(loop_x, minf(y + 5.0, h)), LOOP_LINE, 1.0)
			y += 9.0
		draw_string(font, Vector2(loop_x + 4.0, 11.0), "循环终点", HORIZONTAL_ALIGNMENT_LEFT,
				-1, 9, LOOP_LINE)

	# 关键帧
	if not l.is_empty():
		var tracks: Array = Timeline.tracks_of(l)
		for i in chans.size():
			var channel := String(chans[i])
			var track := Timeline.find_track(tracks, channel)
			if track.is_empty():
				continue
			var keys: Array = track.get("keys", [])
			for k in keys.size():
				var key: Dictionary = keys[k]
				var kt := float(key["t"])
				var pos := Vector2(_x_of(kt), _row_y(i) + ROW_H * 0.5)
				var selected := channel == _selected_channel and k == _selected_index
				_draw_key(pos, selected, kt > loop_t + 0.0001)

	# 空状态提示
	if l.is_empty():
		var text := "选中一个图层后在这里加关键帧"
		if not main.selected_layer().is_empty():
			text = "遮罩层 / 滤镜层的几何是着色器参数，暂不支持关键帧"
		var tw := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
		draw_string(font, Vector2(maxf(8.0, (w - tw) * 0.5), h * 0.5), text,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(0.55, 0.58, 0.64))

	# 播放头
	var now := 0.0
	if main._canvas != null:
		now = main._canvas.time
	var px := _x_of(now)
	draw_line(Vector2(px, 0), Vector2(px, h), PLAYHEAD, 1.5)
	draw_colored_polygon(PackedVector2Array([
		Vector2(px - 5, 0), Vector2(px + 5, 0), Vector2(px, 6),
	]), PLAYHEAD)


func _draw_key(pos: Vector2, selected: bool, out_of_range: bool = false) -> void:
	var r := KEY_RADIUS + (1.5 if selected else 0.0)
	var pts := PackedVector2Array([
		pos + Vector2(0, -r), pos + Vector2(r, 0),
		pos + Vector2(0, r), pos + Vector2(-r, 0),
	])
	var color := KEY_COLOR
	if out_of_range:
		color = KEY_OUT
	if selected:
		color = KEY_SELECTED
	draw_colored_polygon(pts, color)
	var outline := PackedVector2Array([
		pos + Vector2(0, -r), pos + Vector2(r, 0),
		pos + Vector2(0, r), pos + Vector2(-r, 0), pos + Vector2(0, -r),
	])
	draw_polyline(outline, Color(0.08, 0.08, 0.09, 0.9), 1.0)


func _tick_step() -> float:
	var target := length() / 8.0
	for step in [0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0]:
		if float(step) >= target:
			return float(step)
	return 60.0


static func _fmt(t: float) -> String:
	if absf(t - roundf(t)) < 0.001:
		return "%d s" % int(roundf(t))
	return "%.2g s" % t


# ---------------------------------------------------------------- 命中

func _hit_key(pos: Vector2) -> Dictionary:
	var l := layer()
	if l.is_empty() or pos.y < RULER_H:
		return {}
	var row := int((pos.y - RULER_H) / ROW_H)
	var chans := channels()
	if row < 0 or row >= chans.size():
		return {}
	var channel := String(chans[row])
	var track := Timeline.find_track(Timeline.tracks_of(l), channel)
	if track.is_empty():
		return {}
	var keys: Array = track.get("keys", [])
	var best := -1
	var best_dx := HIT_RADIUS
	for i in keys.size():
		var dx := absf(_x_of(float(keys[i]["t"])) - pos.x)
		if dx <= best_dx:
			best_dx = dx
			best = i
	if best < 0:
		return {}
	return {"channel": channel, "index": best}


# ---------------------------------------------------------------- 输入

func _gui_input(event: InputEvent) -> void:
	if main == null or main.doc == null:
		return
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT and mb.pressed:
			if mb.double_click:
				var hit := _hit_key(mb.position)
				if hit.is_empty():
					_add_key_at(mb.position)
				else:
					_seek(_t_of(mb.position.x))
				accept_event()
				return
			if mb.position.y < RULER_H:
				_scrubbing = true
				_seek(_t_of(mb.position.x))
				accept_event()
				return
			var hit2 := _hit_key(mb.position)
			if not hit2.is_empty():
				_selected_channel = String(hit2["channel"])
				_selected_index = int(hit2["index"])
				_drag_channel = _selected_channel
				_drag_index = _selected_index
				_drag_t0 = float(Timeline.find_track(
						Timeline.tracks_of(layer()), _drag_channel)["keys"][_drag_index]["t"])
				_seek(_drag_t0)
				queue_redraw()
				accept_event()
				return
			_seek(_t_of(mb.position.x))
			accept_event()
		elif mb.button_index == MOUSE_BUTTON_LEFT and not mb.pressed:
			if not _drag_channel.is_empty():
				_drag_channel = ""
				_drag_index = -1
				# 松手时做一次完整刷新：关键帧改动可能改变画面，重建能保证与导出一致。
				main.apply_change(false)
				accept_event()
			elif _scrubbing:
				_scrubbing = false
				accept_event()
		elif mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			var hit3 := _hit_key(mb.position)
			if not hit3.is_empty():
				_delete_key(hit3)
				accept_event()
	elif event is InputEventMouseMotion:
		if not _drag_channel.is_empty():
			_drag_key((event as InputEventMouseMotion).position,
					(event as InputEventMouseMotion).shift_pressed)
			accept_event()
		elif _scrubbing:
			_seek(_t_of((event as InputEventMouseMotion).position.x))
			accept_event()


func _seek(t: float) -> void:
	if main != null:
		main.seek_time(t)
	queue_redraw()


func _drag_key(pos: Vector2, free: bool) -> void:
	var l := layer()
	if l.is_empty():
		return
	var track := Timeline.find_track(Timeline.tracks_of(l), _drag_channel)
	if track.is_empty():
		return
	var keys: Array = track.get("keys", [])
	if _drag_index < 0 or _drag_index >= keys.size():
		return
	var t := _t_of(pos.x)
	if not free:
		t = roundf(t / SNAP) * SNAP
	# 不越过相邻关键帧（越过去会改变曲线形状，不是"拖动"的预期）。
	var lo := 0.0
	var hi := loop_length()
	if _drag_index > 0:
		lo = float(keys[_drag_index - 1]["t"]) + SNAP
	if _drag_index < keys.size() - 1:
		hi = float(keys[_drag_index + 1]["t"]) - SNAP
	t = clampf(t, maxf(0.0, lo), hi)
	keys[_drag_index]["t"] = t
	main.mark_dirty()
	# key 的时刻变了：先重装曲线，再把播放头放到它上面（否则这两步会各自 seek 旧曲线）。
	main.recompile_animation()
	main.seek_time(t)
	queue_redraw()


func _add_key_at(pos: Vector2) -> void:
	var l := layer()
	if l.is_empty() or pos.y < RULER_H:
		return
	var row := int((pos.y - RULER_H) / ROW_H)
	var chans := channels()
	if row < 0 or row >= chans.size():
		return
	var channel := String(chans[row])
	var t := _t_of(pos.x)
	# 循环之外的关键帧不会播放，直接不允许在这里新增（拖时长导致的不算）。
	if t > loop_length() + 0.0001:
		return
	var parent_size: Vector2 = main.doc.parent_size(main.sel)
	var value := Timeline.value_at(l, channel, t, parent_size)
	var index := Timeline.set_key(l, channel, t, value, "linear")
	_selected_channel = channel
	_selected_index = index
	main.mark_dirty()
	main.apply_change(false)
	main.seek_time(t)
	queue_redraw()


func _delete_key(hit: Dictionary) -> void:
	var l := layer()
	if l.is_empty():
		return
	if Timeline.remove_key(l, String(hit["channel"]), int(hit["index"])):
		_selected_channel = ""
		_selected_index = -1
		main.mark_dirty()
		main.apply_change(false)
		queue_redraw()


## 删除当前选中的关键帧（Del 键走这里）。
func delete_selected() -> bool:
	if _selected_channel.is_empty():
		return false
	_delete_key({"channel": _selected_channel, "index": _selected_index})
	return true


func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey):
		return
	var key := event as InputEventKey
	if key.pressed and not key.echo and key.keycode == KEY_DELETE:
		if delete_selected():
			accept_event()
