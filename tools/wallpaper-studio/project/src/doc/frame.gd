extends RefCounted
## 图层选区框（Frame）：元素的位置与尺寸。
##
## 文档存**画布像素**：transform.x / y 是左上角位置，w / h 是未旋转时的宽高。
## 场景里却用**锚点**表达（像素 / 画布），所以导出的壁纸和"铺满画布"的图层一样
## 跟着窗口等比伸缩，而不是钉死在某个像素尺寸上。
##
## 全部是纯函数 —— 拖手柄到底改哪个字段、改多少都在这里算，能被无头用例直接钉住；
## 界面层只负责把鼠标位移换算成"本地像素增量"再喂进来。
##
## 历史形态：没有 w/h（= 0）的图层语义是"铺满画布"。of() 统一折算成画布尺寸，
## 调用方（场景构造 / 画布 / 属性面板）只看到一种形态，不用各自判断。

const MIN_SIZE := 8.0

## 手柄用位掩码：动哪几条边就置哪几位，角点 = 相邻两位同时置。
const LEFT := 1
const RIGHT := 2
const TOP := 4
const BOTTOM := 8
## 绘制与命中顺序：左上、上、右上、右、右下、下、左下、左。
const HANDLES := [
	LEFT | TOP, TOP, RIGHT | TOP, RIGHT,
	RIGHT | BOTTOM, BOTTOM, LEFT | BOTTOM, LEFT,
]


## 元素在画布上的矩形（未旋转）。没设过尺寸的图层就是整张画布。
static func of(layer: Dictionary, canvas: Vector2i) -> Rect2:
	var t: Dictionary = layer.get("transform", {})
	var pos := Vector2(float(t.get("x", 0.0)), float(t.get("y", 0.0)))
	var w := float(t.get("w", 0.0))
	var h := float(t.get("h", 0.0))
	if w <= 0.0 or h <= 0.0:
		return Rect2(pos, Vector2(canvas))
	return Rect2(pos, Vector2(w, h))


## 把"铺满画布"落成显式框。第一次拖动/改宽高时调用，之后文档里就有确定的数字。
static func materialize(layer: Dictionary, canvas: Vector2i) -> void:
	var t: Dictionary = layer.get("transform", {})
	if t.is_empty():
		t = {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0,
				"rotation": 0.0, "opacity": 1.0, "scale": 1.0}
		layer["transform"] = t
	if float(t.get("w", 0.0)) <= 0.0 or float(t.get("h", 0.0)) <= 0.0:
		t["w"] = float(canvas.x)
		t["h"] = float(canvas.y)


static func set_rect(layer: Dictionary, r: Rect2) -> void:
	var t: Dictionary = layer["transform"]
	t["x"] = r.position.x
	t["y"] = r.position.y
	t["w"] = r.size.x
	t["h"] = r.size.y


## 文档像素 -> 场景锚点（0..1）。offsets 一律为 0，由场景构造方写进节点。
static func anchors(layer: Dictionary, canvas: Vector2i) -> Dictionary:
	var r := of(layer, canvas)
	var cw := maxf(1.0, float(canvas.x))
	var ch := maxf(1.0, float(canvas.y))
	return {
		"left": r.position.x / cw,
		"top": r.position.y / ch,
		"right": (r.position.x + r.size.x) / cw,
		"bottom": (r.position.y + r.size.y) / ch,
	}


## 位移。delta 是画布像素（未旋转）。
static func move(layer: Dictionary, canvas: Vector2i, delta: Vector2) -> void:
	var r := of(layer, canvas)
	set_rect(layer, Rect2(r.position + delta, r.size))


## 改尺寸。delta 是**未旋转的本地位移**（像素）；handle 决定动哪几条边，
## 拉左边/上边时位置也一起动（对边保持不动）。keep_ratio 只对角点有效。
static func resize(layer: Dictionary, canvas: Vector2i, handle: int, delta: Vector2,
		keep_ratio: bool) -> void:
	var r := of(layer, canvas)
	set_rect(layer, resized_rect(r, handle, delta, keep_ratio))


## 纯矩形版本：拖手柄之后的新矩形（已经夹过最小尺寸）。
static func resized_rect(r: Rect2, handle: int, delta: Vector2, keep_ratio: bool) -> Rect2:
	var x0 := r.position.x
	var y0 := r.position.y
	var x1 := r.position.x + r.size.x
	var y1 := r.position.y + r.size.y
	if (handle & LEFT) != 0:
		x0 = minf(x0 + delta.x, x1 - MIN_SIZE)
	elif (handle & RIGHT) != 0:
		x1 = maxf(x1 + delta.x, x0 + MIN_SIZE)
	if (handle & TOP) != 0:
		y0 = minf(y0 + delta.y, y1 - MIN_SIZE)
	elif (handle & BOTTOM) != 0:
		y1 = maxf(y1 + delta.y, y0 + MIN_SIZE)
	var out := Rect2(Vector2(x0, y0), Vector2(x1 - x0, y1 - y0))
	var has_x := (handle & (LEFT | RIGHT)) != 0
	var has_y := (handle & (TOP | BOTTOM)) != 0
	if keep_ratio and has_x and has_y and r.size.y > 0.0:
		out = _fit_ratio(r, out, handle)
	return out


## 等比：以变化更大的那根轴为准，锚住对角点。
static func _fit_ratio(r: Rect2, nr: Rect2, handle: int) -> Rect2:
	var ratio := r.size.x / r.size.y
	var dw := absf(nr.size.x - r.size.x)
	var dh := absf(nr.size.y - r.size.y) * ratio
	var w := nr.size.x
	if dh > dw:
		w = nr.size.y * ratio
	w = maxf(MIN_SIZE, w)
	var h := maxf(MIN_SIZE, w / ratio)
	var x := nr.position.x
	var y := nr.position.y
	if (handle & LEFT) != 0:
		x = r.position.x + r.size.x - w
	if (handle & TOP) != 0:
		y = r.position.y + r.size.y - h
	return Rect2(Vector2(x, y), Vector2(w, h))


## 点是否落在元素里（考虑旋转与缩放）。point 是画布像素。
static func hit_test(layer: Dictionary, canvas: Vector2i, point: Vector2,
		rotation_deg: float, scale: float) -> bool:
	var r := of(layer, canvas)
	var s := maxf(0.0001, scale)
	var local := (point - r.position).rotated(-deg_to_rad(rotation_deg)) / s
	return Rect2(Vector2.ZERO, r.size).has_point(local)


## 手柄对应的鼠标指针，直接用 Control 的内建游标。
static func cursor_for(handle: int) -> int:
	if handle == (LEFT | TOP) or handle == (RIGHT | BOTTOM):
		return Control.CURSOR_FDIAGSIZE
	if handle == (RIGHT | TOP) or handle == (LEFT | BOTTOM):
		return Control.CURSOR_BDIAGSIZE
	if handle == LEFT or handle == RIGHT:
		return Control.CURSOR_HSIZE
	if handle == TOP or handle == BOTTOM:
		return Control.CURSOR_VSIZE
	return Control.CURSOR_MOVE


## 历史文档用统一 scale 表达缩放。折进显式宽高后，编辑器里只剩"改框"一种模型，
## 渲染结果不变（两者都是以左上角为原点放大）。只在加载时调用一次。
static func migrate_scale(layer: Dictionary, canvas: Vector2i) -> void:
	var t: Dictionary = layer.get("transform", {})
	if t.is_empty():
		return
	if float(t.get("w", 0.0)) > 0.0 and float(t.get("h", 0.0)) > 0.0:
		return
	var s := float(t.get("scale", 1.0))
	if is_equal_approx(s, 1.0):
		return
	t["w"] = float(canvas.x) * s
	t["h"] = float(canvas.y) * s
	t["scale"] = 1.0
