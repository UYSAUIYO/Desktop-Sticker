extends RefCounted
## 壁纸文档。JSON 是唯一真源；所有编辑都改这个对象，预览/导出从它派生。
## 加载策略：任何字段缺失或类型错误都取默认值，只有 format_version 无法识别才失败。
##
## 图层是**树**：`layers` 是根数组（index 0 = 最底层），容器图层有自己的 children。
## 所有"某一层"的寻址都用**路径** PackedInt32Array（如 [2, 0] = 第 3 个根层的第一个子层）。
## 结构化编辑（增删、上下移、移入/移出容器）都在这里，界面只调这些方法。
##
## 遮罩/滤镜是**效果图层**（type = mask / filter），不是图层属性：它们与普通图层并列，
## 用 cover 决定覆盖下方哪几层。见 Layer.is_effect() 与 cover_span()。

const StudioPaths = preload("res://src/doc/paths.gd")
const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const Mask = preload("res://src/doc/mask.gd")
const Filters = preload("res://src/doc/filters.gd")

const FORMAT_VERSION := 1
const MIN_CANVAS := 64

var name: String = "未命名壁纸"
var canvas: Vector2i = Vector2i(1920, 1080)
var background: Color = Color(0.02, 0.07, 0.12)
var pack_id: String = ""
var layers: Array = []          # Array[Dictionary]，index 0 = 最底层


## 不用 class_name 就没法写 `<脚本>.new()` 之外的类型名，统一走这两个入口。
static func new_instance() -> RefCounted:
	return preload("res://src/doc/doc.gd").new()


static func create_empty(title: String, size: Vector2i) -> RefCounted:
	var d = new_instance()
	d.name = title
	d.canvas = size
	d.pack_id = StudioPaths.new_pack_id()
	return d


## 返回 {ok: bool, doc, error: String}
static func parse(text: String) -> Dictionary:
	var json := JSON.new()
	if json.parse(text) != OK:
		return {"ok": false, "doc": null,
				"error": "JSON 解析失败：第 %d 行 %s" % [json.get_error_line(), json.get_error_message()]}

	var raw = json.data
	if not (raw is Dictionary):
		return {"ok": false, "doc": null, "error": "wallpaper.json 的顶层不是对象"}

	var version := int(raw.get("format_version", 0))
	if version < 1 or version > FORMAT_VERSION:
		return {"ok": false, "doc": null,
				"error": "不支持的文档版本 %d（本编辑器支持到 %d），请升级桌面贴纸" % [version, FORMAT_VERSION]}

	var d = new_instance()
	d.name = String(raw.get("name", "未命名壁纸"))
	d.pack_id = String(raw.get("pack_id", ""))
	if d.pack_id.is_empty():
		d.pack_id = StudioPaths.new_pack_id()
	if raw.get("canvas") is Dictionary:
		var c: Dictionary = raw["canvas"]
		d.canvas = Vector2i(int(c.get("width", 1920)), int(c.get("height", 1080)))
	d.background = Layer.to_color(raw.get("background"), Color(0.02, 0.07, 0.12))
	if d.canvas.x < MIN_CANVAS or d.canvas.y < MIN_CANVAS:
		return {"ok": false, "doc": null, "error": "画布尺寸过小：%s" % d.canvas}

	if raw.get("layers") is Array:
		for item in raw["layers"]:
			if item is Dictionary:
				# _migrate 返回的是**一串**图层：老的"每层自带遮罩/滤镜"会被拆成
				# 紧跟在该层上面的独立效果图层（见 _legacy_effects）。
				d.layers.append_array(_migrate(item, Vector2(d.canvas)))

	return {"ok": true, "doc": d, "error": ""}


## 规范化 + 历史迁移。返回 [{layer}, …]：图层本身，外加老字段折算出的效果图层。
## 容器里的子图层按容器框做参照，所以这里要递归下去。
static func _migrate(raw: Dictionary, parent_size: Vector2) -> Array:
	var own := raw.duplicate()
	own.erase("children")
	var layer := Layer.normalize(own)
	Frame.migrate_scale(layer, parent_size)
	if Layer.is_group(layer):
		var kids := []
		var raw_kids = raw.get("children")
		var frame_size := Frame.of(layer, parent_size).size
		if raw_kids is Array:
			for raw_child in raw_kids:
				if raw_child is Dictionary:
					kids.append_array(_migrate(raw_child, frame_size))
		layer["children"] = kids
	var out := [layer]
	out.append_array(_legacy_effects(layer, raw, parent_size))
	return out


## 旧文档把遮罩/滤镜挂在图层自己身上（"节点属性"模型）。加载时把它们拆成
## 紧跟在该层上方的独立效果图层，覆盖层数 = 1，画出来的效果保持一致。
static func _legacy_effects(layer: Dictionary, raw: Dictionary, parent_size: Vector2) -> Array:
	var out := []
	if not bool(layer.get("visible", true)):
		return out
	# 效果图层自己的 filters 字段是它**自己的参数**，不是旧文档的"图层属性"，
	# 不能再拆一层（否则每读一次工程就多出一个滤镜层）。
	if Layer.is_effect(layer):
		return out
	var lf := Frame.of(layer, parent_size)

	var old_mask = raw.get("mask")
	if old_mask is Dictionary and bool(old_mask.get("enabled", false)):
		var m := Layer.make_mask("遮罩")
		var mw := float(old_mask.get("w", 0.0))
		var mh := float(old_mask.get("h", 0.0))
		var mpos := Vector2(float(old_mask.get("x", 0.0)), float(old_mask.get("y", 0.0)))
		var msize := Vector2(mw, mh) if (mw > 0.0 and mh > 0.0) else lf.size
		m["transform"]["x"] = lf.position.x + mpos.x
		m["transform"]["y"] = lf.position.y + mpos.y
		m["transform"]["w"] = msize.x
		m["transform"]["h"] = msize.y
		m["cover"] = {"mode": "count", "count": 1}
		var shape := String(old_mask.get("shape", "rect"))
		m["shape"] = shape if shape in Mask.SHAPES else "rect"
		m["radius"] = maxf(0.0, float(old_mask.get("radius", 24.0)))
		m["feather"] = maxf(0.0, float(old_mask.get("feather", 0.0)))
		m["invert"] = bool(old_mask.get("invert", false))
		m["src"] = String(old_mask.get("src", ""))
		out.append(Layer.normalize(m))

	var f := Filters.normalize(raw.get("filters"))
	if Filters.any(f):
		var fl := Layer.make_filter("滤镜")
		fl["filters"] = f
		fl["cover"] = {"mode": "count", "count": 1}
		out.append(Layer.normalize(fl))
	return out


func to_text() -> String:
	var out := {
		"format_version": FORMAT_VERSION,
		"name": name,
		"pack_id": pack_id,
		"canvas": {"width": canvas.x, "height": canvas.y},
		"background": "#" + background.to_html(false),
		"layers": layers,
	}
	return JSON.stringify(out, "  ")


# ---------------------------------------------------------------- 路径访问

func layer_at(path: PackedInt32Array) -> Dictionary:
	if path.is_empty():
		return {}
	var arr: Array = layers
	var layer: Dictionary = {}
	for i in path.size():
		var idx: int = path[i]
		if idx < 0 or idx >= arr.size():
			return {}
		layer = arr[idx]
		if i < path.size() - 1:
			var kids = layer.get("children")
			if not (kids is Array):
				return {}
			arr = kids
	return layer


## 某个父路径下的兄弟数组（根路径 = []= layers）。
func siblings_at(parent_path: PackedInt32Array) -> Array:
	if parent_path.is_empty():
		return layers
	var parent := layer_at(parent_path)
	if parent.is_empty():
		return []
	var kids = parent.get("children")
	return kids if kids is Array else []


func parent_path_of(path: PackedInt32Array) -> PackedInt32Array:
	return path.slice(0, maxi(0, path.size() - 1))


## 某个父路径对应的父级尺寸：根是画布，容器是它的框。
func parent_size_of(parent_path: PackedInt32Array) -> Vector2:
	if parent_path.is_empty():
		return Vector2(canvas)
	var layer := layer_at(parent_path)
	if layer.is_empty():
		return Vector2(canvas)
	return Frame.of(layer, parent_size_of(parent_path_of(parent_path))).size


## 某图层自己 transform 的参照尺寸（父级尺寸）。
func parent_size(path: PackedInt32Array) -> Vector2:
	return parent_size_of(parent_path_of(path))


## 深度优先展开：父在前、子随后；同一层里 index 0 先。
## 返回 [{path, layer, depth}]。out 不能写成默认值 []：GDScript 的默认数组是共享的，
## 连续两次调用会把上一个结果也带进来（踩过）。
func walk(parent_path = null, depth: int = 0, out = null) -> Array:
	var parent: PackedInt32Array = PackedInt32Array() if parent_path == null else parent_path
	var acc: Array = [] if out == null else out
	var arr := siblings_at(parent)
	for i in arr.size():
		var path := parent.duplicate()
		path.append(i)
		var layer: Dictionary = arr[i]
		acc.append({"path": path, "layer": layer, "depth": depth})
		if Layer.is_group(layer):
			walk(path, depth + 1, acc)
	return acc


## 效果层 path 覆盖的兄弟区间 [from, to)（to = 它自己的下标）。
##
## 语义与场景构造完全一致：只看**它下方**真正生成过节点的兄弟 —— 隐藏的效果层不算，
## 「覆盖不到任何图层的效果层」也不算（它在场景里根本不出现），所以位置编号和
## SceneBuilder 里那张 `built` 列表一一对应。cover.mode = "all" 时 from = 0。
## 绑定模式（"layer"）没有兄弟区间，返回空区间；用 cover_target_path 解析。
func cover_span(path: PackedInt32Array) -> Vector2i:
	if path.is_empty():
		return Vector2i.ZERO
	var parent := parent_path_of(path)
	var arr := siblings_at(parent)
	var idx: int = path[path.size() - 1]
	if idx < 0 or idx >= arr.size():
		return Vector2i.ZERO
	var effect: Dictionary = arr[idx]
	if not Layer.is_effect(effect):
		return Vector2i(idx, idx)
	if not Layer.cover_target(effect).is_empty() \
			or String(effect.get("cover", {}).get("mode", "")) == "layer":
		return Vector2i(idx, idx)
	var slots := []          # 可覆盖兄弟的下标，自下而上
	var m := 0
	for i in idx:
		if _built_weight(arr, i, m) > 0:
			slots.append(i)
			m += 1
	var span := Layer.cover_range(effect, m)
	if span.y < span.x:
		return Vector2i(idx, idx)
	# 位置 a..b（1 = 紧邻效果层）-> arr 下标。位置越大离效果层越远 = 下标越小。
	var lo: int = slots[m - span.y]
	var hi: int = slots[m - span.x]
	return Vector2i(lo, hi + 1)


## 绑定模式的目标图层路径（"这一层只作用在哪一层上"）。
##
## 只能绑**同一组兄弟里的内容图层**：效果层的坐标空间就是它所在父级的，
## 跨容器绑定会让遮罩框（按本层父级存）和画面对不上，所以直接禁止；
## 想给容器里的元素加效果，就把效果层加进那个容器（选中元素再「＋ 添加」即可）。
## 不合法的绑定一律返回空（= 效果层不生效，界面上会写"目标已删除/未选择"）。
func cover_target_path(path: PackedInt32Array) -> PackedInt32Array:
	if path.is_empty():
		return PackedInt32Array()
	var effect: Dictionary = layer_at(path)
	if effect.is_empty() or not Layer.is_effect(effect):
		return PackedInt32Array()
	var want := Layer.cover_target(effect)
	if want.is_empty():
		return PackedInt32Array()
	var parent := parent_path_of(path)
	var arr := siblings_at(parent)
	for i in arr.size():
		var l = arr[i]
		if not (l is Dictionary):
			continue
		if String((l as Dictionary).get("id", "")) != want:
			continue
		if Layer.is_effect(l):
			return PackedInt32Array()      # 效果层没有内容可包
		var out := parent.duplicate()
		out.append(i)
		return out
	return PackedInt32Array()


## 按 id 找图层路径（id 全局唯一；找不到返回空路径）。
func path_of_id(id: String) -> PackedInt32Array:
	if id.is_empty():
		return PackedInt32Array()
	for item in walk():
		if String((item["layer"] as Dictionary).get("id", "")) == id:
			return item["path"]
	return PackedInt32Array()


## 效果层占用的坐标空间 = 被覆盖内容所在的父级。绑定模式下是**目标层的父级**
## （绑定限同一组兄弟，所以两者其实相同；保留成函数是为了让调用方不必关心模式）。
## 遮罩框、合成区域的 UV 都按这个尺寸换算；返回空路径表示这一层此刻不生效。
func effect_space_path(path: PackedInt32Array) -> PackedInt32Array:
	if path.is_empty():
		return path
	var layer: Dictionary = layer_at(path)
	if layer.is_empty():
		return PackedInt32Array()
	if not Layer.is_effect(layer):
		return path
	if String(layer.get("cover", {}).get("mode", "")) == "layer":
		return cover_target_path(path)
	return path


## 第 i 个兄弟在场景里是否占一个"可被覆盖"的位置（1 = 占，0 = 不占）。
## m = 它下方已经占了多少个位置（效果层要按这个数算自己的覆盖区间）。
## 绑定模式的效果层在自己位置上不生成节点（它包的是目标那一层），所以不占位置 ——
## 这一点由 Layer.cover_range("layer") 返回空区间保证，下面的"第 N 层"才数得对。
func _built_weight(arr: Array, i: int, m: int) -> int:
	var s = arr[i]
	if not (s is Dictionary):
		return 0
	if not Layer.is_effect(s):
		return 1
	if not bool(s.get("visible", true)):
		return 0
	var span := Layer.cover_range(s, m)
	return 1 if span.y >= span.x else 0


## 效果层下方有多少层**可被覆盖**（= 属性面板里"起点/终点"的上限）。
func cover_capacity(path: PackedInt32Array) -> int:
	if path.is_empty():
		return 0
	var arr := siblings_at(parent_path_of(path))
	var idx: int = path[path.size() - 1]
	if idx < 0 or idx > arr.size():
		return 0
	var m := 0
	for i in mini(idx, arr.size()):
		m += _built_weight(arr, i, m)
	return m


## 覆盖了多少层（给界面显示"覆盖下方 N 层"）。绑定模式恒为 1（目标在才算）。
func covered_count(path: PackedInt32Array) -> int:
	if _is_bound(path):
		return 0 if cover_target_path(path).is_empty() else 1
	var span := cover_span(path)
	return maxi(0, span.y - span.x)


## 被覆盖图层的名字，自上而下（属性面板里让用户确认"覆盖的到底是哪几层"）。
func covered_names(path: PackedInt32Array) -> Array:
	var out := []
	if _is_bound(path):
		var target := cover_target_path(path)
		if not target.is_empty():
			out.append(String(layer_at(target).get("name", "图层")))
		return out
	var arr := siblings_at(parent_path_of(path))
	var span := cover_span(path)
	for i in range(span.y - 1, span.x - 1, -1):
		if i >= 0 and i < arr.size() and (arr[i] is Dictionary):
			out.append(String(arr[i].get("name", "图层")))
	return out


## 被覆盖内容在**父级像素**里的包围盒。打开「限制作用区域」时用它给框一个初始值：
## 画面上先不变（框正好等于现在被处理的那块），用户再往小/往别处拖。
## 没有可覆盖内容时退化成整个父级（= 不限制，和 Frame.of 对 w/h = 0 的语义一致）。
func covered_bounds(path: PackedInt32Array) -> Rect2:
	if path.is_empty():
		return Rect2()
	var parent_size := parent_size_of(parent_path_of(path))
	var items := []
	var target := cover_target_path(path)
	if not target.is_empty():
		items.append({"layer": layer_at(target)})
	else:
		var arr := siblings_at(parent_path_of(path))
		var span := cover_span(path)
		for i in range(span.x, span.y):
			if i >= 0 and i < arr.size() and (arr[i] is Dictionary):
				items.append({"layer": arr[i]})
	var r := Layer.bounds_of(items, parent_size)
	if r.size.x <= 0.0 or r.size.y <= 0.0:
		return Rect2(Vector2.ZERO, parent_size)
	return r


## 滤镜层在画布上的框（父级像素）。滤镜层**一直有框**，和图片/遮罩层一样能拖能缩：
##   · 固定范围（region.enabled）—— 框就是它自己的 transform，滤镜只在这个框内生效；
##   · 跟随被覆盖图层（默认）—— 框 = 被覆盖内容的包围盒，跟着内容走，滤镜作用在整块内容上。
## 用户第一次拖这个框 / 改 X·Y·宽·高 时由 fix_filter_region() 落成固定范围。
func filter_frame(path: PackedInt32Array) -> Rect2:
	if path.is_empty():
		return Rect2()
	var layer: Dictionary = layer_at(path)
	if Layer.is_filter(layer) and not Filters.region_enabled(layer):
		return covered_bounds(path)
	return Frame.of(layer, parent_size(path))


## 把滤镜层的框从"跟随"落成"固定"：框 = 当前被覆盖内容的包围盒，区域同时生效。
## 因为框正好等于现在被处理的那块，画面不变 —— 用户接着拖/缩放即可。
func fix_filter_region(path: PackedInt32Array) -> void:
	if path.is_empty():
		return
	var layer: Dictionary = layer_at(path)
	if not Layer.is_filter(layer):
		return
	var b := covered_bounds(path)
	Frame.materialize(layer, parent_size(path))
	Frame.set_rect(layer, b)
	var rg := Filters.region(layer)
	rg["enabled"] = true
	layer["region"] = rg


## 这一层是不是"绑定到指定图层"的效果层（不管目标还在不在）。
func _is_bound(path: PackedInt32Array) -> bool:
	if path.is_empty():
		return false
	var layer: Dictionary = layer_at(path)
	return Layer.is_effect(layer) and String(layer.get("cover", {}).get("mode", "")) == "layer"


# ---------------------------------------------------------------- 结构编辑

## 在 parent_path 下插入图层。index < 0 表示追加到最上层。
## 返回新图层的路径。
##
## 注意别拿"子数组为空"当"父级不是容器"的判断：空的容器是合法的（刚建的容器就是空的），
## 那样会把新层静默加回根层（踩过）。
func insert_layer(parent_path: PackedInt32Array, layer: Dictionary,
		index: int = -1) -> PackedInt32Array:
	var normalized := Layer.normalize(layer)
	var arr: Array = layers
	var parent := PackedInt32Array()
	if not parent_path.is_empty():
		var pl := layer_at(parent_path)
		var kids = pl.get("children")
		if kids is Array:
			arr = kids
			parent = parent_path
	var at := arr.size() if index < 0 else clampi(index, 0, arr.size())
	arr.insert(at, normalized)
	var path := parent.duplicate()
	path.append(at)
	return path


func remove_at(path: PackedInt32Array) -> Dictionary:
	if path.is_empty():
		return {}
	var parent_path := parent_path_of(path)
	var arr := siblings_at(parent_path)
	if arr.is_empty():
		arr = layers
	var idx: int = path[path.size() - 1]
	if idx < 0 or idx >= arr.size():
		return {}
	var layer: Dictionary = arr[idx]
	arr.remove_at(idx)
	return layer


## 在兄弟之间上下移动（delta = +1 更靠前 / -1 更靠后）。返回新路径。
func move_within(path: PackedInt32Array, delta: int) -> PackedInt32Array:
	if path.is_empty():
		return path
	var parent_path := parent_path_of(path)
	var arr := siblings_at(parent_path)
	if arr.is_empty():
		arr = layers
	var from: int = path[path.size() - 1]
	var to := from + delta
	if from < 0 or from >= arr.size() or to < 0 or to >= arr.size():
		return path
	var item = arr[from]
	arr.remove_at(from)
	arr.insert(to, item)
	var out := parent_path.duplicate()
	out.append(to)
	return out


## 把图层移到另一个父级下，保持它在屏幕上的位置。
func reparent(path: PackedInt32Array, new_parent: PackedInt32Array) -> PackedInt32Array:
	if path.is_empty():
		return path
	if new_parent.size() >= path.size() and new_parent.slice(0, path.size()) == path:
		return path                     # 不能塞进自己（或自己的子孙）里
	var layer := layer_at(path)
	if layer.is_empty():
		return path
	var old_map := Frame.space_map(self, path)
	var probe := new_parent.duplicate()
	probe.append(0)
	var new_map := Frame.space_map(self, probe)
	var fr := Frame.of(layer, parent_size(path))
	var root_pos: Vector2 = old_map["origin"] \
			+ (fr.position * float(old_map["scale"])).rotated(deg_to_rad(float(old_map["rotation"])))
	var local := Frame.to_space(root_pos, new_map)
	remove_at(path)
	if float(layer["transform"].get("w", 0.0)) <= 0.0:
		Frame.materialize(layer, fr.size)
	layer["transform"]["x"] = local.x
	layer["transform"]["y"] = local.y
	var arr: Array = layers
	var parent := PackedInt32Array()
	if not new_parent.is_empty():
		var pl := layer_at(new_parent)
		var kids = pl.get("children")
		if kids is Array:
			arr = kids
			parent = new_parent
	arr.append(layer)
	var out := parent.duplicate()
	out.append(arr.size() - 1)
	return out


## 把一个图层包进新建的容器（容器框 = 图层当前的框）。返回容器路径。
func wrap_in_group(path: PackedInt32Array) -> PackedInt32Array:
	var layer := layer_at(path)
	if layer.is_empty() or Layer.is_effect(layer):
		return path
	var parent_path := parent_path_of(path)
	var size := parent_size(path)
	var fr := Frame.of(layer, size)
	var group := Layer.make_group(String(layer.get("name", "图层")) + " 容器",
			fr.size.x, fr.size.y)
	group["transform"]["x"] = fr.position.x
	group["transform"]["y"] = fr.position.y
	var t: Dictionary = layer["transform"]
	t["x"] = 0.0
	t["y"] = 0.0
	t["w"] = fr.size.x
	t["h"] = fr.size.y
	var idx: int = path[path.size() - 1]
	var arr := siblings_at(parent_path)
	if arr.is_empty():
		arr = layers
	arr.remove_at(idx)
	group["children"] = [layer]
	arr.insert(idx, Layer.normalize(group))
	var out := parent_path.duplicate()
	out.append(idx)
	return out


## 画布尺寸变更。scale_layers = 按比例缩放所有元素（等比拉伸内容），否则保持像素不变。
func set_canvas(new_size: Vector2i, scale_layers: bool = true) -> void:
	var old := Vector2(canvas)
	var nx := maxi(MIN_CANVAS, new_size.x)
	var ny := maxi(MIN_CANVAS, new_size.y)
	if scale_layers and old.x > 0.0 and old.y > 0.0:
		var sx := float(nx) / old.x
		var sy := float(ny) / old.y
		if not is_equal_approx(sx, 1.0) or not is_equal_approx(sy, 1.0):
			layers = Frame.scale_children(layers, sx, sy, Vector2.ZERO)
	canvas = Vector2i(nx, ny)


# ---------------------------------------------------------------- 兼容旧接口

func add_layer(layer: Dictionary) -> void:
	layers.append(Layer.normalize(layer))


func remove_layer(index: int) -> void:
	if index >= 0 and index < layers.size():
		layers.remove_at(index)


func move_layer(from_index: int, to_index: int) -> void:
	if from_index < 0 or from_index >= layers.size():
		return
	if to_index < 0 or to_index >= layers.size():
		return
	var item = layers[from_index]
	layers.remove_at(from_index)
	layers.insert(to_index, item)


# ---------------------------------------------------------------- 导出

## 导出时要写进 manifest.content_flags 的东西：用户自己带来的脚本。
## 不含 Studio 生成的时钟脚本 —— 那是内在的、纯展示的，不算用户的脚本。
func external_scripts() -> Array:
	var out := []
	for item in walk():
		var p := String((item["layer"] as Dictionary).get("script_path", ""))
		if not p.is_empty():
			out.append(p)
	return out


## 导出前校验。返回问题列表（中文，直接给用户看）。
## project_dir 用于检查引用到的资源是否存在。
func validate(project_dir: String) -> PackedStringArray:
	var problems := PackedStringArray()
	if name.strip_edges().is_empty():
		problems.append("壁纸名称为空。")
	if canvas.x < MIN_CANVAS or canvas.y < MIN_CANVAS:
		problems.append("画布尺寸过小：%s。" % canvas)
	var visible := 0
	for item in walk():
		var l: Dictionary = item["layer"]
		if not bool(l.get("visible", true)):
			continue
		if Layer.is_content(l):
			visible += 1
		var type := String(l["type"])
		var rel := ""
		if type == "image":
			rel = String(l.get("src", ""))
			if rel.is_empty():
				problems.append("图层「%s」没有选择图片。" % l["name"])
		elif type == "particles":
			rel = String(l.get("texture", ""))
		elif type == "mask" and String(l.get("shape", "")) == "image":
			rel = String(l.get("src", ""))
			if rel.is_empty():
				problems.append("遮罩层「%s」没有选择遮罩图。" % l["name"])
		if Layer.is_effect(l) and not Layer.cover_target(l).is_empty() \
				and cover_target_path(item["path"]).is_empty():
			problems.append("效果层「%s」绑定的图层已经不存在（或不是可绑定的图层），这一层不会生效。" % l["name"])
		if not rel.is_empty() and not FileAccess.file_exists(project_dir.path_join(rel)):
			problems.append("图层「%s」的资源不存在：%s" % [l["name"], rel])
		if int(l.get("amount", 1)) < 1:
			problems.append("图层「%s」的粒子数量必须大于 0。" % l["name"])
	if visible == 0:
		problems.append("没有任何可见图层，导出的会是一张纯色图。")
	return problems


## 导出要拷进包里的一切实体资源（图片层、粒子贴图、图片遮罩）。
## 返回 [{layer, field, rel}]，由 exporter 决定怎么拷。
func asset_refs() -> Array:
	var out := []
	for item in walk():
		var l: Dictionary = item["layer"]
		if not bool(l.get("visible", true)):
			continue
		var type := String(l["type"])
		if type == "image":
			var rel := String(l.get("src", ""))
			if not rel.is_empty():
				out.append({"layer": l, "field": "src", "rel": rel})
		elif type == "particles":
			var rel2 := String(l.get("texture", ""))
			if not rel2.is_empty():
				out.append({"layer": l, "field": "texture", "rel": rel2})
		elif type == "mask" and String(l.get("shape", "")) == "image":
			var msrc := String(l.get("src", ""))
			if not msrc.is_empty():
				out.append({"layer": l, "field": "src", "rel": msrc})
	return out
