extends RefCounted
## 时间轴：**纯函数**，不读盘、不碰节点、不依赖其它模块（避免 preload 环）。
##
## 图层上的 `tracks` 是唯一真源，形态与设计 §3.4 一致：
##   [{"channel": "x", "keys": [{"t": 0.0, "v": 0.0, "ease": "linear"}, …]}, …]
##
## 通道语义（都只有**一个**浮点数，所以关键帧是一维的）：
##   x / y      —— 位置，**父级比例**（0..1，可越界）：0.5 表示父级宽度的一半。
##                 和静态图层的锚点是同一个量，所以换画布尺寸 / 缩放容器时不用改关键帧，
##                 运行时也天然按分辨率伸缩。
##   scale      —— 整体缩放倍数，以框的左上角为原点（1.0 = 原始大小）
##   rotation   —— 旋转角度（度，正值顺时针）
##   opacity    —— 不透明度 0..1（直接覆盖基础值）
##
## 历史（format_version 1）：x/y 存的是父级**像素**。加载时一次性折算成比例（见
## doc.gd 的 _convert_tracks_to_ratio），之后所有代码只认比例。
##
## 采样的边界规则：首帧之前取首值，末帧之后**保持末值**（不循环回绕，回绕交给
## 播放器按 loop_seconds 处理）；只有一个关键帧时恒值。
##
## 本模块刻意不认识 Frame：位置/尺寸的兜底算法就三行（见 base_pose / frame_size），
## 抄一份比引出一层依赖更省事，也避免 layer -> timeline -> frame -> layer 的环。

const CHANNELS := ["x", "y", "scale", "rotation", "opacity"]
const CHANNEL_NAMES := {
	"x": "X 位置",
	"y": "Y 位置",
	"scale": "缩放",
	"rotation": "旋转",
	"opacity": "不透明度",
}
const EASES := ["linear", "in", "out", "in_out"]
const EASE_NAMES := {
	"linear": "线性",
	"in": "缓入",
	"out": "缓出",
	"in_out": "缓入缓出",
}
## 两个关键帧的时间差小于它就算同一时刻（拖拽吸附后会出现 1e-6 级的抖动）。
const EPS := 0.0005


static func default_value(channel: String) -> float:
	if channel == "scale" or channel == "opacity":
		return 1.0
	return 0.0


## 图层的静态基准姿态。没有关键帧的通道一律回落到这里。
## x/y 一并折算成父级比例 —— 下游（采样 / 场景构造 / 命中测试）只处理一种单位。
static func base_pose(layer: Dictionary, parent_size: Vector2) -> Dictionary:
	var t: Dictionary = layer.get("transform", {})
	return {
		"x": float(t.get("x", 0.0)) / maxf(1.0, parent_size.x),
		"y": float(t.get("y", 0.0)) / maxf(1.0, parent_size.y),
		"scale": float(t.get("scale", 1.0)),
		"rotation": float(t.get("rotation", 0.0)),
		"opacity": float(t.get("opacity", 1.0)),
	}


## 界面单位（父级像素）<-> 文档单位（比例）。x/y 走这一对；
## scale / rotation / opacity 本来就没有量纲，原样返回。
static func to_doc(channel: String, px: float, parent_size: Vector2) -> float:
	match channel:
		"x":
			return px / maxf(1.0, parent_size.x)
		"y":
			return px / maxf(1.0, parent_size.y)
	return px


static func to_px(channel: String, value: float, parent_size: Vector2) -> float:
	match channel:
		"x":
			return value * maxf(1.0, parent_size.x)
		"y":
			return value * maxf(1.0, parent_size.y)
	return value


## 框的宽高：没设过尺寸（w/h = 0）的图层铺满父级。
static func frame_size(layer: Dictionary, parent_size: Vector2) -> Vector2:
	var t: Dictionary = layer.get("transform", {})
	var w := float(t.get("w", 0.0))
	var h := float(t.get("h", 0.0))
	if w <= 0.0:
		w = float(parent_size.x)
	if h <= 0.0:
		h = float(parent_size.y)
	return Vector2(maxf(1.0, w), maxf(1.0, h))


# ---------------------------------------------------------------- 规范化

## 把文档里读到的 tracks 洗成合法形态：通道未知/关键帧全坏 -> 整条丢掉。
## 任何字段缺失或类型不对都取默认值，**不因为多出字段而失败**（与 doc.parse 同一策略）。
static func normalize_tracks(raw) -> Array:
	var out := []
	if not (raw is Array):
		return out
	for item in raw:
		if not (item is Dictionary):
			continue
		var track := normalize_track(item)
		if not track.is_empty():
			out.append(track)
	return out


static func normalize_track(raw: Dictionary) -> Dictionary:
	var channel := String(raw.get("channel", ""))
	if not (channel in CHANNELS):
		return {}
	var keys := []
	if raw.get("keys") is Array:
		for k in raw["keys"]:
			if not (k is Dictionary):
				continue
			var ease := String(k.get("ease", "linear"))
			if not (ease in EASES):
				ease = "linear"
			keys.append({
				"t": maxf(0.0, float(k.get("t", 0.0))),
				"v": float(k.get("v", default_value(channel))),
				"ease": ease,
			})
	if keys.is_empty():
		return {}
	keys.sort_custom(func(a, b) -> bool: return float(a["t"]) < float(b["t"]))
	var dedup := []
	for key in keys:
		if not dedup.is_empty() \
				and absf(float(dedup[dedup.size() - 1]["t"]) - float(key["t"])) < EPS:
			dedup[dedup.size() - 1] = key
		else:
			dedup.append(key)
	return {"channel": channel, "keys": dedup}


# ---------------------------------------------------------------- 采样

## 单条 track 在 t 时刻的值；没有关键帧返回 null（调用方回落到基准值）。
static func sample_track(track: Dictionary, t: float):
	var keys: Array = track.get("keys", [])
	if keys.is_empty():
		return null
	if t <= float(keys[0]["t"]):
		return float(keys[0]["v"])
	var last: Dictionary = keys[keys.size() - 1]
	if t >= float(last["t"]):
		return float(last["v"])

	# 二分找区间：keys 已按 t 升序（normalize 保证，set_key 也维持）。
	var lo := 0
	var hi := keys.size() - 1
	while hi - lo > 1:
		var mid := (lo + hi) / 2
		if float(keys[mid]["t"]) <= t:
			lo = mid
		else:
			hi = mid
	var a: Dictionary = keys[lo]
	var b: Dictionary = keys[hi]
	var span := float(b["t"]) - float(a["t"])
	if span <= 0.0:
		return float(b["v"])
	var u := clampf((t - float(a["t"])) / span, 0.0, 1.0)
	return lerpf(float(a["v"]), float(b["v"]), ease_value(u, String(b.get("ease", "linear"))))


## 缓动曲线。ease 记在**区间终点**那个关键帧上（"这一段怎么走到这个值"）。
static func ease_value(u: float, mode: String) -> float:
	var x := clampf(u, 0.0, 1.0)
	match mode:
		"in":
			return x * x * x
		"out":
			var inv := 1.0 - x
			return 1.0 - inv * inv * inv
		"in_out":
			return x * x * (3.0 - 2.0 * x)
	return x


## 图层在 t 时刻的完整姿态 {x, y, scale, rotation, opacity}。
## 没有关键帧的通道回落到基准值，所以调用方可以无条件使用返回值。
static func sample_layer(layer: Dictionary, t: float, parent_size: Vector2) -> Dictionary:
	var pose := base_pose(layer, parent_size)
	for track in tracks_of(layer):
		var v = sample_track(track, t)
		if v != null:
			pose[String(track.get("channel", ""))] = float(v)
	return pose


static func tracks_of(layer: Dictionary) -> Array:
	var tracks = layer.get("tracks", [])
	return tracks if tracks is Array else []


static func find_track(tracks: Array, channel: String) -> Dictionary:
	for track in tracks:
		if track is Dictionary and String(track.get("channel", "")) == channel:
			return track
	return {}


static func has_animation(layer: Dictionary) -> bool:
	for track in tracks_of(layer):
		if track is Dictionary and not (track.get("keys", []) as Array).is_empty():
			return true
	return false


## 这一层最后一个关键帧的时刻（没有关键帧返回 0）。
static func end_of(layer: Dictionary) -> float:
	var end := 0.0
	for track in tracks_of(layer):
		for key in track.get("keys", []):
			if key is Dictionary:
				end = maxf(end, float(key.get("t", 0.0)))
	return end


## 所有关键帧时刻（去重后升序）。时间轴面板画刻度、拖拽吸附都用它。
static func key_times(layer: Dictionary) -> Array:
	var out := []
	for track in tracks_of(layer):
		for key in track.get("keys", []):
			if key is Dictionary:
				var t := float(key.get("t", 0.0))
				if not out.has(t):
					out.append(t)
	out.sort()
	return out


# ---------------------------------------------------------------- 编辑

## 在 t 时刻写入一个关键帧（同刻已有则覆盖）。返回它在 keys 里的下标。
## 直接改传进来的 layer 字典 —— 文档就是可变字典，这一层不做 copy。
static func set_key(layer: Dictionary, channel: String, t: float, value: float,
		ease: String = "linear") -> int:
	if not (channel in CHANNELS):
		return -1
	var tracks: Array = layer.get("tracks", [])
	if not (tracks is Array):
		tracks = []
		layer["tracks"] = tracks
	var track := find_track(tracks, channel)
	if track.is_empty():
		track = {"channel": channel, "keys": []}
		tracks.append(track)
	var keys: Array = track.get("keys", [])
	if not (keys is Array):
		keys = []
		track["keys"] = keys

	var at := maxf(0.0, t)
	for i in range(keys.size() - 1, -1, -1):
		if absf(float(keys[i].get("t", 0.0)) - at) < EPS:
			keys.remove_at(i)
	var mode := ease if ease in EASES else "linear"
	keys.append({"t": at, "v": value, "ease": mode})
	keys.sort_custom(func(a, b) -> bool: return float(a["t"]) < float(b["t"]))
	for i in keys.size():
		if absf(float(keys[i]["t"]) - at) < EPS:
			return i
	return keys.size() - 1


## 删掉某条通道上的第 index 个关键帧。通道空了就把整条 track 摘掉（不留下空壳）。
static func remove_key(layer: Dictionary, channel: String, index: int) -> bool:
	var tracks: Array = layer.get("tracks", [])
	var track := find_track(tracks, channel)
	if track.is_empty():
		return false
	var keys: Array = track.get("keys", [])
	if index < 0 or index >= keys.size():
		return false
	keys.remove_at(index)
	if keys.is_empty():
		tracks.erase(track)
	return true


## 把某个通道的关键帧整体清空（面板上的"清除该通道"）。
static func clear_channel(layer: Dictionary, channel: String) -> bool:
	var tracks: Array = layer.get("tracks", [])
	var track := find_track(tracks, channel)
	if track.is_empty():
		return false
	tracks.erase(track)
	return true


## 父级换了（拖进 / 拖出容器、包一层容器）时，把 x/y 关键帧整体搬到新参照系。
## `to_new` 吃**旧父级的像素坐标**、返回**新父级的像素坐标**（可以带旋转/缩放）——
## 直接用 Frame.space_map 拼一个就行。
##
## 只有这一个操作需要搬关键帧：
##   · 改画布尺寸 / 缩放容器 —— x/y 存的是比例，位置和运动幅度自动跟着父级伸缩；
##   · 移动父级 —— 子层的父空间没变，动画自然跟着父级走。
## 这就是把单位从"绝对像素"换成"父级比例"换来的性质。
static func remap_xy(layer: Dictionary, old_size: Vector2, new_size: Vector2,
		to_new: Callable) -> void:
	var tracks := tracks_of(layer)
	var tx := find_track(tracks, "x")
	var ty := find_track(tracks, "y")
	if tx.is_empty() and ty.is_empty():
		return
	var base := base_pose(layer, old_size)
	var seen := {}
	var times := []
	for track in [tx, ty]:
		if track.is_empty():
			continue
		for key in track.get("keys", []):
			var tid := "%.6f" % float(key["t"])
			if not seen.has(tid):
				seen[tid] = true
				times.append({"tid": tid, "t": float(key["t"])})

	var mapped := {}
	for item in times:
		var t: float = item["t"]
		var vx = sample_track(tx, t) if not tx.is_empty() else base["x"]
		var vy = sample_track(ty, t) if not ty.is_empty() else base["y"]
		var p_old := Vector2(to_px("x", float(vx), old_size), to_px("y", float(vy), old_size))
		mapped[String(item["tid"])] = to_new.call(p_old)

	for track in [tx, ty]:
		if track.is_empty():
			continue
		var channel := String(track.get("channel", ""))
		for key in track.get("keys", []):
			var p: Vector2 = mapped["%.6f" % float(key["t"])]
			key["v"] = to_doc(channel, p.x if channel == "x" else p.y, new_size)


## 图层在 t 时刻的**可视矩形**（未旋转，父级像素）。缩放以左上角为原点，
## 所以它只把宽高乘上 scale，位置不变。
## 只给"有动画的图层"用：静态图层仍然走 Frame.of，两边的语义（w/h = 0 铺满父级）
## 由 frame_size 保持一致。
static func rect_at(layer: Dictionary, t: float, parent_size: Vector2) -> Rect2:
	var pose := sample_layer(layer, t, parent_size)
	var size := frame_size(layer, parent_size) * maxf(0.0, float(pose.get("scale", 1.0)))
	return Rect2(Vector2(to_px("x", float(pose.get("x", 0.0)), parent_size),
			to_px("y", float(pose.get("y", 0.0)), parent_size)), size)


## 某条通道在 t 时刻的采样值，**文档单位**（x/y 是父级比例）。时间轴加关键帧走这里，
## 所以它写回去的值和读出来的值始终同单位。
static func value_at(layer: Dictionary, channel: String, t: float, parent_size: Vector2) -> float:
	var pose := sample_layer(layer, t, parent_size)
	return float(pose.get(channel, default_value(channel)))
