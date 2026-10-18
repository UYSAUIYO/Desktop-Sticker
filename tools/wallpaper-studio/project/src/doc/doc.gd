extends RefCounted
## 壁纸文档。JSON 是唯一真源；所有编辑都改这个对象，预览/导出从它派生。
## 加载策略：任何字段缺失或类型错误都取默认值，只有 format_version 无法识别才失败。
##
## 图层是**树**：`layers` 是根数组（index 0 = 最底层），容器图层有自己的 children。
## 所有"某一层"的寻址都用**路径** PackedInt32Array（如 [2, 0] = 第 3 个根层的第一个子层）。
## 结构化编辑（增删、上下移、移入/移出容器）都在这里，界面只调这些方法。

const StudioPaths = preload("res://src/doc/paths.gd")
const Layer = preload("res://src/doc/layer.gd")
const Frame = preload("res://src/doc/frame.gd")
const Mask = preload("res://src/doc/mask.gd")

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
				d.layers.append(_migrate(Layer.normalize(item), Vector2(d.canvas)))

	return {"ok": true, "doc": d, "error": ""}


## 老文档用统一 scale 表达缩放：加载时就折进宽高，之后编辑器里只有"拖框"一种模型。
## 容器里的子图层按容器框做参照，所以这里要递归下去。
static func _migrate(layer: Dictionary, parent_size: Vector2) -> Dictionary:
	Frame.migrate_scale(layer, parent_size)
	if layer.get("children") is Array and not (layer["children"] as Array).is_empty():
		var frame_size := Frame.of(layer, parent_size).size
		var kids := []
		for child in layer["children"]:
			kids.append(_migrate(child, frame_size))
		layer["children"] = kids
	return layer


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
	if layer.is_empty():
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
		if not Layer.is_group(l):
			visible += 1
		var type := String(l["type"])
		var rel := ""
		if type == "image":
			rel = String(l.get("src", ""))
			if rel.is_empty():
				problems.append("图层「%s」没有选择图片。" % l["name"])
		elif type == "particles":
			rel = String(l.get("texture", ""))
		if not rel.is_empty() and not FileAccess.file_exists(project_dir.path_join(rel)):
			problems.append("图层「%s」的资源不存在：%s" % [l["name"], rel])
		if int(l.get("amount", 1)) < 1:
			problems.append("图层「%s」的粒子数量必须大于 0。" % l["name"])
		var mask: Dictionary = l.get("mask", {})
		if bool(mask.get("enabled", false)) and String(mask.get("shape", "")) == "image":
			var msrc := String(mask.get("src", ""))
			if msrc.is_empty():
				problems.append("图层「%s」的图片遮罩没有选择图片。" % l["name"])
			elif not FileAccess.file_exists(project_dir.path_join(msrc)):
				problems.append("图层「%s」的遮罩资源不存在：%s" % [l["name"], msrc])
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
		var mask: Dictionary = l.get("mask", {})
		if bool(mask.get("enabled", false)) and String(mask.get("shape", "")) == "image":
			var msrc := String(mask.get("src", ""))
			if not msrc.is_empty():
				out.append({"layer": l, "field": "mask", "rel": msrc})
	return out
