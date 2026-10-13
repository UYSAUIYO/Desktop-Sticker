extends RefCounted
## 壁纸文档。JSON 是唯一真源；所有编辑都改这个对象，预览/导出从它派生。
## 加载策略：任何字段缺失或类型错误都取默认值，只有 format_version 无法识别才失败。

const StudioPaths = preload("res://src/doc/paths.gd")
const Layer = preload("res://src/doc/layer.gd")

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
				d.layers.append(Layer.normalize(item))

	return {"ok": true, "doc": d, "error": ""}


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


## 导出时要写进 manifest.content_flags 的东西：用户自己带来的脚本。
## 不含 Studio 生成的时钟脚本 —— 那是内在的、纯展示的，不算用户的脚本。
func external_scripts() -> Array:
	var out := []
	for l in layers:
		var p := String(l.get("script_path", ""))
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
	for l in layers:
		if not bool(l.get("visible", true)):
			continue
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
	if visible == 0:
		problems.append("没有任何可见图层，导出的会是一张纯色图。")
	return problems
