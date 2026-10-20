extends RefCounted
## 运动路径（贝塞尔）：让元素**沿曲线**走，而不是只在 X/Y 两条一维轨道上各自插值。
##
## 数据挂在图层上（`layer.motion`），与 `tracks` 并列：
##
##   "motion": {
##     "enabled": true,
##     "start": 0.0, "end": 4.0,          # 走完一整条路径的秒数
##     "points": [                         # >= 2 个点，坐标是**父级比例**
##       {"p": [0.1, 0.5], "in": [...], "out": [...]}, …
##     ]
##   }
##
## 单位为什么也是父级比例（和 x/y 关键帧同一个量）：换画布尺寸、缩放容器时
## 路径跟着一起伸缩，不需要重算；命中测试 / 选区框 / 导出三处也共用这一套。
## 控制柄存的是**绝对坐标**而不是相对锚点的偏移 —— 序列化直白，拖动时也算得简单。
##
## 采样：相邻两点之间是一段三次贝塞尔（P0=点 i、C1=点 i 的 out、C2=点 i+1 的 in、
## P3=点 i+1），段与段之间按时间均分（等速走完，不是按弧长）。
##
## 优先级：路径启用时它**接管 x/y**（x/y 通道的关键帧不再生效）—— 两条一维轨道
## 描述不了曲线，两边同时用只会互相打架。scale / rotation / opacity 照常。
##
## 本文件是纯函数：不 preload 任何模块（timeline 会 preload 它，避免成环）。

const MIN_POINTS := 2
const MAX_POINTS := 32


# ---------------------------------------------------------------- 规范化

static func normalize(raw) -> Dictionary:
	if not (raw is Dictionary):
		return {}
	var d: Dictionary = raw
	var pts := []
	var raw_pts = d.get("points")
	if raw_pts is Array:
		for item in raw_pts:
			if not (item is Dictionary):
				continue
			var p := _vec2_of(item.get("p"), Vector2.ZERO)
			var cin := _vec2_of(item.get("in"), p)
			var cout := _vec2_of(item.get("out"), p)
			pts.append({
				"p": [p.x, p.y],
				"in": [cin.x, cin.y],
				"out": [cout.x, cout.y],
			})
	if pts.size() < MIN_POINTS:
		return {}
	if pts.size() > MAX_POINTS:
		pts = pts.slice(0, MAX_POINTS)
	var start := maxf(0.0, float(d.get("start", 0.0)))
	var end := maxf(start + 0.05, float(d.get("end", start + 4.0)))
	return {
		"enabled": bool(d.get("enabled", true)),
		"start": start,
		"end": end,
		"points": pts,
	}


static func _vec2_of(raw, fallback: Vector2) -> Vector2:
	if raw is Array and (raw as Array).size() >= 2:
		return Vector2(float(raw[0]), float(raw[1]))
	if raw is Vector2:
		return raw
	return fallback


# ---------------------------------------------------------------- 查询

static func motion_of(layer: Dictionary) -> Dictionary:
	var m = layer.get("motion")
	return m if m is Dictionary else {}


static func has(layer: Dictionary) -> bool:
	var m := motion_of(layer)
	return not m.is_empty() and (m.get("points", []) as Array).size() >= MIN_POINTS


## 启用且可用（有点、有时间跨度）。只有它为 true 时路径才接管 x/y。
static func enabled(layer: Dictionary) -> bool:
	var m := motion_of(layer)
	if m.is_empty() or not bool(m.get("enabled", true)):
		return false
	return (m.get("points", []) as Array).size() >= MIN_POINTS


static func points_of(layer: Dictionary) -> Array:
	var m := motion_of(layer)
	var pts = m.get("points", [])
	return pts if pts is Array else []


static func point(layer: Dictionary, i: int) -> Dictionary:
	var pts := points_of(layer)
	if i < 0 or i >= pts.size():
		return {}
	return pts[i]


static func _vec_at(pt: Dictionary, key: String) -> Vector2:
	return _vec2_of(pt.get(key), _vec2_of(pt.get("p"), Vector2.ZERO))


# ---------------------------------------------------------------- 采样

## t 时刻在路径上的位置（**父级比例**）。没启用 / 没路径返回 null（调用方回落）。
static func sample(layer: Dictionary, t: float):
	if not enabled(layer):
		return null
	var pts := points_of(layer)
	if pts.size() < MIN_POINTS:
		return null
	var m := motion_of(layer)
	var start := float(m.get("start", 0.0))
	var end := float(m.get("end", start + 1.0))
	if t <= start:
		return _vec_at(pts[0], "p")
	if t >= end:
		return _vec_at(pts[pts.size() - 1], "p")
	var u := (t - start) / maxf(0.0001, end - start)
	var segs := pts.size() - 1
	var s := clampf(u * float(segs), 0.0, float(segs))
	var i := mini(int(floor(s)), segs - 1)
	var lu := s - float(i)
	var a: Dictionary = pts[i]
	var b: Dictionary = pts[i + 1]
	return _bezier(_vec_at(a, "p"), _vec_at(a, "out"), _vec_at(b, "in"), _vec_at(b, "p"), lu)


## 同上，但返回**父级像素**（界面上画框用）。没路径返回 null。
static func sample_point(layer: Dictionary, t: float):
	var p = sample(layer, t)
	if p == null:
		return null
	return p


static func _bezier(p0: Vector2, c0: Vector2, c1: Vector2, p1: Vector2, u: float) -> Vector2:
	var v := 1.0 - u
	return (v * v * v) * p0 + (3.0 * v * v * u) * c0 + (3.0 * v * u * u) * c1 + (u * u * u) * p1


## 画路径用的折线（父级像素）。steps 段数。
static func polyline(layer: Dictionary, parent_size: Vector2, steps: int = 48) -> Array:
	var out := []
	if not enabled(layer):
		return out
	var m := motion_of(layer)
	var start := float(m.get("start", 0.0))
	var end := float(m.get("end", start + 1.0))
	for i in range(steps + 1):
		var t := lerpf(start, end, float(i) / float(steps))
		var p = sample(layer, t)
		if p == null:
			continue
		out.append(Vector2(float(p.x) * parent_size.x, float(p.y) * parent_size.y))
	return out


## 编译用：把整条路径按 hz 采样成 [{t, x, y}]（x/y 仍是比例）。
## 段数封顶，防止"10 分钟的路径"把动画撑爆（和缓动烘焙同一个上限）。
static func bake(layer: Dictionary, hz: float = 60.0, max_steps: int = 600) -> Array:
	var out := []
	if not enabled(layer):
		return out
	var m := motion_of(layer)
	var start := float(m.get("start", 0.0))
	var end := float(m.get("end", start + 1.0))
	var steps := clampi(int(ceil((end - start) * hz)), 2, max_steps)
	for i in range(steps + 1):
		var t := lerpf(start, end, float(i) / float(steps))
		var p = sample(layer, t)
		if p == null:
			continue
		out.append({"t": t, "x": float(p.x), "y": float(p.y)})
	return out


# ---------------------------------------------------------------- 编辑

## 给图层建一条默认路径：起点 = 现在的位置，终点 = 往右下挪开一点，
## 控制柄水平伸出，所以默认是一条平滑的弧线而不是折线。
static func make_default(layer: Dictionary, parent_size: Vector2) -> Dictionary:
	var t: Dictionary = layer.get("transform", {})
	var pw := maxf(1.0, parent_size.x)
	var ph := maxf(1.0, parent_size.y)
	var x := float(t.get("x", 0.0)) / pw
	var y := float(t.get("y", 0.0)) / ph
	var w := Timeline_frame_w(layer, parent_size) / pw
	var dx := 0.35
	var dy := 0.25
	return normalize({
		"enabled": true,
		"start": 0.0,
		"end": 4.0,
		"points": [
			{"p": [x, y], "in": [x - w * 0.5, y], "out": [x + w * 0.5, y]},
			{"p": [x + dx, y + dy], "in": [x + dx - 0.12, y + dy],
					"out": [x + dx + 0.12, y + dy]},
		],
	})


## 框的宽度（比例）。这里不能 preload timeline（会成环），所以只抄 w/h 兜底那两行。
static func Timeline_frame_w(layer: Dictionary, parent_size: Vector2) -> float:
	var t: Dictionary = layer.get("transform", {})
	var w := float(t.get("w", 0.0))
	return w if w > 0.0 else float(parent_size.x)


static func enable(layer: Dictionary, parent_size: Vector2) -> Dictionary:
	var m := motion_of(layer)
	if not m.is_empty() and (m.get("points", []) as Array).size() >= MIN_POINTS:
		m["enabled"] = true
		return m
	var made := make_default(layer, parent_size)
	layer["motion"] = made
	return made


static func disable(layer: Dictionary) -> void:
	var m := motion_of(layer)
	if not m.is_empty():
		m["enabled"] = false


static func clear(layer: Dictionary) -> void:
	layer.erase("motion")


## 控制点列表（父级像素），供画布上画手柄与拖拽：
## 每个点依次给 锚点（anchor）、out 柄、in 柄（首尾的悬空柄不列）。
static func control_nodes(layer: Dictionary, parent_size: Vector2) -> Array:
	var out := []
	var pts := points_of(layer)
	if pts.size() < MIN_POINTS:
		return out
	for i in pts.size():
		var pt: Dictionary = pts[i]
		var p := _vec_at(pt, "p")
		out.append(_node(p, p, true, "anchor", i, parent_size))
		if i < pts.size() - 1:
			var c := _vec_at(pt, "out")
			out.append(_node(c, p, false, "out", i, parent_size))
		if i > 0:
			var ci := _vec_at(pt, "in")
			out.append(_node(ci, p, false, "in", i, parent_size))
	return out


static func _node(p: Vector2, anchor: Vector2, is_anchor: bool, kind: String,
		index: int, parent_size: Vector2) -> Dictionary:
	return {
		"x": p.x * parent_size.x,
		"y": p.y * parent_size.y,
		"ax": anchor.x * parent_size.x,
		"ay": anchor.y * parent_size.y,
		"anchor": is_anchor,
		"kind": kind,
		"index": index,
	}


## 拖第 index 个控制点（control_nodes 的下标），delta 是**父级比例的位移**。
## 端点：整点连柄一起走；控制柄：对侧的柄镜像跟着转，保持平滑（C1 连续）。
static func move_node(layer: Dictionary, index: int, delta: Vector2) -> bool:
	var pts := points_of(layer)
	if pts.size() < MIN_POINTS:
		return false
	var nodes := control_nodes(layer, Vector2(1.0, 1.0))
	if index < 0 or index >= nodes.size():
		return false
	var n: Dictionary = nodes[index]
	var i: int = int(n["index"])
	var pt: Dictionary = pts[i]
	var kind := String(n["kind"])
	if kind == "anchor":
		pt["p"] = _pair(_vec_at(pt, "p") + delta)
		pt["in"] = _pair(_vec_at(pt, "in") + delta)
		pt["out"] = _pair(_vec_at(pt, "out") + delta)
		return true
	var moved := _vec_at(pt, kind) + delta
	pt[kind] = _pair(moved)
	var anchor := _vec_at(pt, "p")
	var other := "in" if kind == "out" else "out"
	pt[other] = _pair(anchor * 2.0 - moved)      # 镜像
	return true


static func _pair(v: Vector2) -> Array:
	return [v.x, v.y]


## 在末尾追加一个点（位置 = 最后一点往右下挪一点）。
static func add_point(layer: Dictionary, parent_size: Vector2) -> bool:
	var pts := points_of(layer)
	if pts.size() < MIN_POINTS or pts.size() >= MAX_POINTS:
		return false
	var last: Dictionary = pts[pts.size() - 1]
	var p := _vec_at(last, "p")
	var n := Vector2(p.x + 0.2, p.y + 0.15)
	pts.append({"p": _pair(n), "in": _pair(n + Vector2(-0.08, 0.0)),
			"out": _pair(n + Vector2(0.08, 0.0))})
	return true


static func remove_point(layer: Dictionary, i: int) -> bool:
	var pts := points_of(layer)
	if i < 0 or i >= pts.size() or pts.size() <= MIN_POINTS:
		return false
	pts.remove_at(i)
	return true


## 父级换了（拖进 / 拖出容器、包一层容器）：点的单位虽然是父级比例，但**父级本身
## 变了**（画布 1920 -> 容器 400），不换算的话元素会跳。和 x/y 关键帧走同一个
## to_new（旧父级像素 -> 新父级像素），换算后写回比例。
static func remap(layer: Dictionary, old_size: Vector2, new_size: Vector2,
		to_new: Callable) -> void:
	var m := motion_of(layer)
	if m.is_empty():
		return
	var pts := points_of(layer)
	var ox := maxf(1.0, old_size.x)
	var oy := maxf(1.0, old_size.y)
	var nx := maxf(1.0, new_size.x)
	var ny := maxf(1.0, new_size.y)
	for pt in pts:
		for key in ["p", "in", "out"]:
			var r := _vec_at(pt, key)
			var moved: Vector2 = to_new.call(Vector2(r.x * ox, r.y * oy))
			pt[key] = [moved.x / nx, moved.y / ny]
