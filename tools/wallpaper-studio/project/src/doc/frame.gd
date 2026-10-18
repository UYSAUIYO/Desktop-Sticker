extends RefCounted
## 图层选区框（Frame）：元素的位置与尺寸。
##
## 文档存**父级像素**：transform.x / y 是左上角位置，w / h 是未旋转时的宽高。
## 根图层（以及容器里的子图层）都以**父级尺寸**为参照——根图层的父级就是画布，
## 子图层的父级是容器的框。于是移动/缩放容器时，子图层自动跟着走。
##
## 场景里却用**锚点**表达（像素 / 父级尺寸），所以导出的壁纸和"铺满画布"的图层一样
## 跟着窗口等比伸缩，而不是钉死在某个像素尺寸上。
##
## 全部是纯函数 —— 拖手柄到底改哪个字段、改多少都在这里算，能被无头用例直接钉住；
## 界面层只负责把鼠标位移换算成"本地像素增量"再喂进来。
##
## 历史形态：没有 w/h（= 0）的图层语义是"铺满父级"。of() 统一折算成父级尺寸，
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


## 元素在父级里的矩形（未旋转）。没设过尺寸的图层就是整个父级。
## parent_size 允许传 Vector2i（画布）或 Vector2（容器框）。
static func of(layer: Dictionary, parent_size) -> Rect2:
	var t: Dictionary = layer.get("transform", {})
	var pos := Vector2(float(t.get("x", 0.0)), float(t.get("y", 0.0)))
	var w := float(t.get("w", 0.0))
	var h := float(t.get("h", 0.0))
	if w <= 0.0 or h <= 0.0:
		return Rect2(pos, Vector2(float(parent_size.x), float(parent_size.y)))
	return Rect2(pos, Vector2(w, h))


## 把"铺满父级"落成显式框。第一次拖动/改宽高时调用，之后文档里就有确定的数字。
static func materialize(layer: Dictionary, parent_size) -> void:
	var t: Dictionary = layer.get("transform", {})
	if t.is_empty():
		t = {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0,
				"rotation": 0.0, "opacity": 1.0, "scale": 1.0}
		layer["transform"] = t
	if float(t.get("w", 0.0)) <= 0.0 or float(t.get("h", 0.0)) <= 0.0:
		t["w"] = float(parent_size.x)
		t["h"] = float(parent_size.y)


static func set_rect(layer: Dictionary, r: Rect2) -> void:
	var t: Dictionary = layer["transform"]
	t["x"] = r.position.x
	t["y"] = r.position.y
	t["w"] = r.size.x
	t["h"] = r.size.y


## 文档像素 -> 场景锚点（0..1）。offsets 一律为 0，由场景构造方写进节点。
static func anchors(layer: Dictionary, parent_size) -> Dictionary:
	var r := of(layer, parent_size)
	var cw := maxf(1.0, float(parent_size.x))
	var ch := maxf(1.0, float(parent_size.y))
	return {
		"left": r.position.x / cw,
		"top": r.position.y / ch,
		"right": (r.position.x + r.size.x) / cw,
		"bottom": (r.position.y + r.size.y) / ch,
	}


## 位移。delta 是父级像素（未旋转）。
static func move(layer: Dictionary, parent_size, delta: Vector2) -> void:
	var r := of(layer, parent_size)
	set_rect(layer, Rect2(r.position + delta, r.size))


## 改尺寸。delta 是**未旋转的本地位移**（像素）；handle 决定动哪几条边，
## 拉左边/上边时位置也一起动（对边保持不动）。keep_ratio 只对角点有效。
static func resize(layer: Dictionary, parent_size, handle: int, delta: Vector2,
		keep_ratio: bool) -> void:
	var r := of(layer, parent_size)
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


## 拖这个手柄时哪一角不动（缩放子元素要以它为锚）。
static func anchor_point(handle: int, r: Rect2) -> Vector2:
	var x := r.position.x
	var y := r.position.y
	if (handle & LEFT) != 0:
		x = r.position.x + r.size.x
	if (handle & TOP) != 0:
		y = r.position.y + r.size.y
	return Vector2(x, y)


## 点是否落在元素里（考虑旋转）。point 是**父级像素**。
static func hit_test(layer: Dictionary, parent_size, point: Vector2, rotation_deg: float) -> bool:
	var r := of(layer, parent_size)
	var local := (point - r.position).rotated(-deg_to_rad(rotation_deg))
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


# ---------------------------------------------------------------- 层级坐标

## path 指向的图层，它的 transform 所在空间（父空间）-> 根空间（画布像素）的映射。
## 返回 {origin: Vector2, scale: float, rotation: 度}。
##
## 只做位移 + 等比缩放 + 旋转累加，不做剪切：本编辑器里容器缩放是"等比拉伸内容"，
## 每次拖拽都会把子元素的框按比例改写（见 scale_children），所以不会产生非等比矩阵。
static func space_map(doc, path: PackedInt32Array) -> Dictionary:
	var out := {"origin": Vector2.ZERO, "scale": 1.0, "rotation": 0.0}
	if path.size() <= 1:
		return out
	var parent_path := path.slice(0, path.size() - 1)
	var pp := space_map(doc, parent_path)
	var parent_layer: Dictionary = doc.layer_at(parent_path)
	if parent_layer.is_empty():
		return pp
	var psize: Vector2 = doc.parent_size(parent_path)
	var fr := of(parent_layer, psize)
	out["origin"] = pp["origin"] + (fr.position * float(pp["scale"])).rotated(
			deg_to_rad(float(pp["rotation"])))
	out["scale"] = pp["scale"]
	out["rotation"] = float(pp["rotation"]) + float(
			parent_layer.get("transform", {}).get("rotation", 0.0))
	return out


## 图层**自身**空间（本地像素，遮罩就是这套坐标）-> 根空间。
static func layer_map(doc, path: PackedInt32Array) -> Dictionary:
	var m := space_map(doc, path)
	var layer: Dictionary = doc.layer_at(path)
	if layer.is_empty():
		return m
	var fr := of(layer, doc.parent_size(path))
	return {
		"origin": m["origin"] + (fr.position * float(m["scale"])).rotated(
				deg_to_rad(float(m["rotation"]))),
		"scale": m["scale"],
		"rotation": float(m["rotation"]) + float(
				layer.get("transform", {}).get("rotation", 0.0)),
	}


## 根空间点 -> 某个空间里的点。
static func to_space(root_point: Vector2, map: Dictionary) -> Vector2:
	return (root_point - map["origin"]).rotated(-deg_to_rad(float(map["rotation"]))) \
			/ maxf(0.0001, float(map["scale"]))


## 等比缩放一组子图层（改容器尺寸时用）。anchor 是容器本地坐标里不动的那个点。
## 容器里的子元素按同样的比例拉伸 —— 这就是"画布跟着走"的效果。
## 注意它是纯函数：返回新数组，调用方自己决定要不要替换。
static func scale_children(children: Array, sx: float, sy: float, anchor: Vector2) -> Array:
	var out := []
	for raw in children:
		var c: Dictionary = raw.duplicate(true)
		var t: Dictionary = c.get("transform", {})
		if t.is_empty():
			out.append(c)
			continue
		var x := float(t.get("x", 0.0))
		var y := float(t.get("y", 0.0))
		t["x"] = anchor.x + (x - anchor.x) * sx
		t["y"] = anchor.y + (y - anchor.y) * sy
		if float(t.get("w", 0.0)) > 0.0:
			t["w"] = maxf(MIN_SIZE, float(t["w"]) * sx)
		if float(t.get("h", 0.0)) > 0.0:
			t["h"] = maxf(MIN_SIZE, float(t["h"]) * sy)
		c["transform"] = t
		# 遮罩是图层本地像素，跟着同一套比例走。
		var mask: Dictionary = c.get("mask", {})
		if not mask.is_empty() and float(mask.get("w", 0.0)) > 0.0:
			mask["x"] = float(mask.get("x", 0.0)) * sx
			mask["y"] = float(mask.get("y", 0.0)) * sy
			mask["w"] = float(mask.get("w", 0.0)) * sx
			mask["h"] = float(mask.get("h", 0.0)) * sy
			mask["feather"] = float(mask.get("feather", 0.0)) * minf(sx, sy)
		if c.get("children") is Array and not (c["children"] as Array).is_empty():
			# 子容器里的内容按同一比例拉伸（相对它自己的原点）。
			c["children"] = scale_children(c["children"], sx, sy, Vector2.ZERO)
		out.append(c)
	return out


## 历史文档用统一 scale 表达缩放。折进显式宽高后，编辑器里只剩"改框"一种模型，
## 渲染结果不变（两者都是以左上角为原点放大）。只在加载时调用一次。
static func migrate_scale(layer: Dictionary, parent_size) -> void:
	var t: Dictionary = layer.get("transform", {})
	if t.is_empty():
		return
	if float(t.get("w", 0.0)) > 0.0 and float(t.get("h", 0.0)) > 0.0:
		return
	var s := float(t.get("scale", 1.0))
	if is_equal_approx(s, 1.0):
		return
	t["w"] = float(parent_size.x) * s
	t["h"] = float(parent_size.y) * s
	t["scale"] = 1.0
