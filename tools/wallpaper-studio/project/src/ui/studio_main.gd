extends Control
## 界面总装。唯一持有 StudioDoc 的地方；面板只通过它读写文档并发信号。
##
## 与设计 §2.3 一致：本工程不用 class_name（运行库里没有全局类缓存），
## 依赖一律 const + preload；任何被 preload 的脚本里都不要写 static var。
##
## 布局（自上而下）：
##   ProjectBar （36px，新建/打开/保存/导出）
##   HBox       （LayerPanel 240 | CanvasView 自适应 | Inspector 300）
##   Timeline   （底部停靠，约 190px）

const StudioDoc = preload("res://src/doc/doc.gd")
const StudioPaths = preload("res://src/doc/paths.gd")
const History = preload("res://src/doc/history.gd")
const Layer = preload("res://src/doc/layer.gd")
const ProjectBar = preload("res://src/ui/project_bar.gd")
const LayerPanel = preload("res://src/ui/layer_panel.gd")
const CanvasView = preload("res://src/ui/canvas_view.gd")
const Inspector = preload("res://src/ui/inspector.gd")
const TimelinePanel = preload("res://src/ui/timeline_panel.gd")
const Preview3D = preload("res://src/ui/preview3d.gd")
const ObjectsPanel = preload("res://src/ui/objects_panel.gd")
const Inspector3D = preload("res://src/ui/inspector3d.gd")
const Scene3D = preload("res://src/doc/scene3d.gd")

signal doc_changed
signal selection_changed

var doc = null                 # StudioDoc 实例（RefCounted，无类型标注是有意的）
var project_dir := ""          # 用户工程目录（绝对路径）
## 当前选中图层的**路径**（空 = 未选）：[2] = 第 3 个根层，[2, 0] = 它的第一个子层。
## 多选时它是"主选择"：属性面板 / 时间轴显示它，其余的在 sel_more 里。
var sel := PackedInt32Array()
var sel_more: Array = []         # Array[PackedInt32Array]，附加选择（不含主选择）
var dirty := false             # 有没有未保存的改动
var busy := false              # 导出等阻塞操作进行中：禁止新建/打开/退出

var _history = null              # History 实例（快照栈，见 doc/history.gd）
var _clipboard: Array = []       # 复制的图层（深拷贝，粘贴时重新分配 id）
var _clipboard3d: Array = []     # 复制的 3D 对象（同上）

var _bar: Control
var _layers: Control
var _canvas: Control
var _inspector: Control
var _timeline: Control
# 3D 场景模式（mode = "3d"）的三块面板；2D 模式下整块隐藏，见 _apply_mode()。
var _objects: Control
var _preview3d: Control
var _inspector3d: Control


func _ready() -> void:
	# 有未保存改动时由我们自己弹确认框，不能让引擎直接关窗。
	get_tree().auto_accept_quit = false
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_install_fonts()
	_build_ui()

	var fresh = StudioDoc.create_empty("未命名壁纸", Vector2i(1920, 1080))
	# 不预先创建工程目录：每次启动都建一个空的「未命名壁纸」会在 projects/ 里堆垃圾。
	# 目录在第一次保存（或第一次导入图片）时才创建，见 ensure_project_dir()。
	_set_doc(fresh, "")
	_refresh_all()


# ---------------------------------------------------------------- 面板装配

func _build_ui() -> void:
	var vbox := VBoxContainer.new()
	vbox.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	vbox.add_theme_constant_override("separation", 0)
	add_child(vbox)

	_bar = ProjectBar.new()
	_bar.custom_minimum_size = Vector2(0, 36)
	vbox.add_child(_bar)

	var hbox := HBoxContainer.new()
	hbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hbox.add_theme_constant_override("separation", 1)
	vbox.add_child(hbox)

	_layers = LayerPanel.new()
	_layers.custom_minimum_size = Vector2(240, 0)
	hbox.add_child(_layers)

	_canvas = CanvasView.new()
	_canvas.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_canvas.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hbox.add_child(_canvas)

	_inspector = Inspector.new()
	_inspector.custom_minimum_size = Vector2(300, 0)
	hbox.add_child(_inspector)

	# 3D 模式面板：默认隐藏，打开 3D 工程时由 _apply_mode 换上来。
	_objects = ObjectsPanel.new()
	_objects.custom_minimum_size = Vector2(240, 0)
	_objects.visible = false
	hbox.add_child(_objects)

	_preview3d = Preview3D.new()
	_preview3d.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_preview3d.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_preview3d.visible = false
	hbox.add_child(_preview3d)

	_inspector3d = Inspector3D.new()
	_inspector3d.custom_minimum_size = Vector2(300, 0)
	_inspector3d.visible = false
	hbox.add_child(_inspector3d)

	_timeline = TimelinePanel.new()
	_timeline.custom_minimum_size = Vector2(0, 190)
	vbox.add_child(_timeline)

	# setup 放在全部节点进树之后：面板的 _ready 只建自己的控件，不依赖 main。
	_bar.setup(self)
	_layers.setup(self)
	_canvas.setup(self)
	_inspector.setup(self)
	_timeline.setup(self)
	_objects.setup(self)
	_preview3d.setup(self)
	_inspector3d.setup(self)


## 默认字体。运行库内置字体没有中文字形，不换字体的话所有中文都是方块。
## 同时写 ThemeDB.fallback_font：对话框是独立 Window，不吃根 Control 的 theme。
func _install_fonts() -> void:
	var font := SystemFont.new()
	font.font_names = PackedStringArray([
		"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC",
		"Noto Sans CJK SC", "Segoe UI",
	])
	font.allow_system_fallback = true
	ThemeDB.fallback_font = font
	ThemeDB.fallback_font_size = 14

	var th := Theme.new()
	th.default_font = font
	th.default_font_size = 14
	theme = th


# ---------------------------------------------------------------- 文档生命周期

func _set_doc(new_doc, dir_path: String) -> void:
	doc = new_doc
	project_dir = dir_path
	sel = PackedInt32Array()
	sel_more = []
	dirty = false
	# 换工程 = 历史归零：撤销不能退到上一个工程里去。
	_reset_history()
	# 换工程 = 时间轴归零：新工程不该停在上一个工程拖到的位置。
	if _canvas != null:
		_canvas.time = 0.0
		_canvas.playing = false
	if _preview3d != null:
		_preview3d.time = 0.0
		_preview3d.playing = false
		_preview3d.camera_view = false
	_apply_mode()
	_update_window_title()


## 按 doc.mode 切换 2D / 3D 面板组。两套面板都常驻（只切可见性），
## 这样来回切换不用重建任何东西，选择状态也各归各的。
func _apply_mode() -> void:
	var is3d: bool = doc != null and doc.is_3d()
	_layers.visible = not is3d
	_canvas.visible = not is3d
	_inspector.visible = not is3d
	_objects.visible = is3d
	_preview3d.visible = is3d
	_inspector3d.visible = is3d
	# 3D 的时间轴（相机路径）在预览自己的工具条上，底部面板对 3D 没有意义。
	_timeline.visible = not is3d


## 新工程目录：user://projects/<安全名>；同名已存在就加 " 2"、" 3" …
func _make_project_dir(title: String) -> String:
	var root := ProjectSettings.globalize_path(StudioPaths.default_project_root())
	DirAccess.make_dir_recursive_absolute(root)
	var base := StudioPaths.safe_name(title)
	var path := root.path_join(base)
	var n := 2
	while (FileAccess.file_exists(path.path_join("wallpaper.json"))
			or DirAccess.dir_exists_absolute(path.path_join("assets"))):
		path = root.path_join("%s %d" % [base, n])
		n += 1
	DirAccess.make_dir_recursive_absolute(path.path_join("assets"))
	return path


func new_project(title: String, size: Vector2i, scene_mode: String = "2d") -> void:
	if busy:
		return
	var fresh = StudioDoc.create_empty(title, size)
	fresh.mode = "3d" if scene_mode == "3d" else "2d"
	if fresh.is_3d():
		fresh.objects3d = Scene3D.starter_scene()
	_set_doc(fresh, "")
	_refresh_all()


## 当前工程还没有目录时（新建但没保存过）按文档名建一个，返回绝对路径。
## 保存、导入图片、导出都要先走这里 —— 否则导入的资源没有地方放。
func ensure_project_dir() -> String:
	if not project_dir.is_empty():
		return project_dir
	if doc == null:
		return ""
	project_dir = _make_project_dir(String(doc.name))
	if _inspector != null:
		_inspector.refresh(true)
	if _bar != null:
		_bar.update_status()
	return project_dir


func request_new_project(title: String, size: Vector2i) -> void:
	confirm_discard(func() -> void: new_project(title, size))


func open_project(dir_path: String) -> bool:
	if busy:
		return false
	var manifest := dir_path.path_join("wallpaper.json")
	if not FileAccess.file_exists(manifest):
		show_error("无法打开工程", "这个目录里没有 wallpaper.json：\n" + dir_path)
		return false
	var parsed: Dictionary = StudioDoc.parse(FileAccess.get_file_as_string(manifest))
	if not parsed.ok:
		show_error("无法打开工程", String(parsed.error))
		return false
	_set_doc(parsed.doc, dir_path)
	_refresh_all()
	return true


func request_open_project(dir_path: String) -> void:
	confirm_discard(func() -> void: open_project(dir_path))


func save_project() -> bool:
	if doc == null:
		return false
	var dir := ensure_project_dir()
	if dir.is_empty():
		show_error("保存失败", "无法创建工程目录（user:// 不可写？）。")
		return false
	DirAccess.make_dir_recursive_absolute(dir.path_join("assets"))
	var path := dir.path_join("wallpaper.json")
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		show_error("保存失败", "无法写入：\n" + path)
		return false
	f.store_string(doc.to_text())
	f.close()
	dirty = false
	_bar.update_status()
	_update_window_title()
	return true


# ---------------------------------------------------------------- 面板回调

## 任何面板改了文档之后走这里：重建画布与图层列表，可选重建属性面板。
## label 会记进撤销历史（同一种连续操作靠它合并成一步）。
func apply_change(rebuild_inspector: bool = false, label: String = "编辑") -> void:
	if doc == null:
		return
	dirty = true
	doc_changed.emit()
	if doc.is_3d():
		if _preview3d != null:
			_preview3d.rebuild()
		if _objects != null:
			_objects.refresh()
		if _inspector3d != null:
			_inspector3d.refresh(true)
	else:
		if _canvas != null:
			_canvas.rebuild()
		if _layers != null:
			_layers.refresh()
		if rebuild_inspector and _inspector != null:
			_inspector.refresh(true)
	if _timeline != null:
		_timeline.sync_time()
		_timeline.refresh_warnings()
	if _bar != null:
		_bar.update_status()
	_update_window_title()
	commit(label)


# ---------------------------------------------------------------- 撤销 / 重做

func _reset_history() -> void:
	if _history == null:
		_history = History.new()
	_history.reset(_snapshot())
	_sync_history()


## 把"现在的文档"记进历史。内容没变不记（面板刷新也走 apply_change）；
## 同一种操作在 0.7 秒内连续发生会合并成一步（拖滑块 / 拖关键帧）。
func commit(label: String = "编辑") -> void:
	if doc == null:
		return
	if _history == null:
		_history = History.new()
	_history.push(_snapshot(), label)
	_sync_history()


## 下一步提交不参与合并：一段新的连续操作开始了（比如鼠标按下）。
func break_history() -> void:
	if _history != null:
		_history.break_merge()


func can_undo() -> bool:
	return _history != null and _history.can_undo()


func can_redo() -> bool:
	return _history != null and _history.can_redo()


func undo() -> void:
	if _history == null or not _history.can_undo():
		return
	_restore(_history.undo())


func redo() -> void:
	if _history == null or not _history.can_redo():
		return
	_restore(_history.redo())


func _snapshot() -> Dictionary:
	var more := []
	for p in sel_more:
		more.append((p as PackedInt32Array).duplicate())
	return {"text": doc.to_text() if doc != null else "", "sel": sel.duplicate(),
			"more": more}


func _restore(state: Dictionary) -> void:
	if state.is_empty() or doc == null:
		return
	var parsed: Dictionary = StudioDoc.parse(String(state.get("text", "")))
	if not parsed.ok:
		return
	doc = parsed.doc
	var p: PackedInt32Array = state.get("sel", PackedInt32Array())
	var camera_sel: bool = doc.is_3d() and p.size() == 1 and int(p[0]) == ObjectsPanel.CAMERA_PATH
	if not p.is_empty() and not camera_sel and doc.layer_at(p).is_empty():
		p = PackedInt32Array()
	sel = p
	sel_more = []
	for item in state.get("more", []):
		var mp: PackedInt32Array = item as PackedInt32Array
		if not mp.is_empty() and not doc.layer_at(mp).is_empty():
			sel_more.append(mp)
	dirty = true
	_refresh_all()
	_sync_history()


func _sync_history() -> void:
	if _bar != null and _bar.has_method("sync_history"):
		_bar.sync_history()


# ---------------------------------------------------------------- 选择（多选）


## 关键帧被就地改过（拖动时间轴上 key 的时刻）：只重装播放器里的曲线，不重建节点。
func recompile_animation() -> void:
	if _canvas != null:
		_canvas.recompile_animation()


func seek_time(t: float) -> void:
	if doc != null and doc.is_3d():
		if _preview3d != null:
			_preview3d.set_time(t)
		return
	if _canvas == null:
		return
	_canvas.set_time(t)
	if _timeline != null:
		_timeline.sync_time()
	if _inspector != null:
		_inspector.sync_time()


func toggle_play() -> void:
	if doc != null and doc.is_3d():
		if _preview3d != null:
			_preview3d.toggle_play()
		return
	if _canvas == null:
		return
	_canvas.toggle_play()
	if _timeline != null:
		_timeline.sync_play_button()
	if _inspector != null:
		_inspector.sync_time()


## 播放中属性面板的数值要跟着播放头走（只改值，不重建面板）。
func _process(_delta: float) -> void:
	if _canvas != null and _canvas.playing and _inspector != null:
		_inspector.sync_time()


## 拖动类交互的轻量路径：只改画布上那个节点的位置/尺寸，不重建整棵树
## （每个鼠标移动事件都重造节点树的话，拖起来会一顿一顿的）。
## 松手后走 apply_change(true) 做完整刷新。
func apply_live() -> void:
	if doc == null:
		return
	dirty = true
	if doc.is_3d():
		if _preview3d != null:
			_preview3d.apply_live(sel)
		return
	if _canvas != null:
		_canvas.apply_live(sel)


## 当前选中的图层（未选/路径失效时返回空字典）。
func selected_layer() -> Dictionary:
	if doc == null or sel.is_empty():
		return {}
	return doc.layer_at(sel)


## 选中图层。index 是根层下标（旧的调用方式），等价于路径 [index]。
func select_layer(index: int) -> void:
	if index < 0:
		select_layer_path(PackedInt32Array())
		return
	select_layer_path(PackedInt32Array([index]))


## 按路径选中（容器里的子层也能选）。空路径 = 取消选择。
## keep_more = true 时保留附加选择（框选 / 加选用），否则清空。
func select_layer_path(path: PackedInt32Array, keep_more: bool = false) -> void:
	if doc == null:
		return
	if doc.is_3d():
		# 3D 里 sel 是 objects3d 的下标（[-1] 是相机的哨兵），不是图层路径 ——
		# 走 layer_at 校验会把每一次选择都判成"路径失效"而清空。
		if path.size() == 1 and int(path[0]) != ObjectsPanel.CAMERA_PATH:
			var oi := int(path[0])
			if oi < 0 or oi >= doc.objects3d.size():
				path = PackedInt32Array()
	elif not path.is_empty() and doc.layer_at(path).is_empty():
		path = PackedInt32Array()
	if not keep_more:
		sel_more = []
	elif path.is_empty():
		sel_more = []
	sel = path
	if _layers != null:
		_layers.sync_selection()
	if doc != null and doc.is_3d() and _objects != null:
		_objects.sync_selection()
		if _inspector3d != null:
			_inspector3d.refresh(true)
	_canvas_changed_selection()
	selection_changed.emit()


## 当前选中的 3D 物体（未选 / 选的是相机 / 路径失效时返回空字典）。
func selected_object() -> Dictionary:
	if doc == null or not doc.is_3d() or sel.size() != 1:
		return {}
	var i := int(sel[0])
	if i < 0 or i >= doc.objects3d.size():
		return {}
	return doc.objects3d[i]


## 3D 的粘贴：复制体放在原物体旁（+1 米），名字带后缀，id 换新。
func _paste_objects(offset := Vector2(1.0, 0.0)) -> void:
	if doc == null or _clipboard3d.is_empty():
		return
	var pasted := []
	for raw in _clipboard3d:
		if not (raw is Dictionary):
			continue
		var copy: Dictionary = (raw as Dictionary).duplicate(true)
		copy["id"] = "o_" + "%08x" % (randi() & 0xFFFFFFFF)
		copy["name"] = String(copy.get("name", "物体")) + " 副本"
		copy["pos"] = [float(copy["pos"][0]) + offset.x, float(copy["pos"][1]),
				float(copy["pos"][2])]
		doc.objects3d.append(copy)
		pasted.append(PackedInt32Array([doc.objects3d.size() - 1]))
	if pasted.is_empty():
		return
	sel = pasted[pasted.size() - 1]
	apply_change(true, "粘贴对象")
	selection_changed.emit()


## 所有选中路径（主选择在前），已过滤失效项。
func selected_paths() -> Array:
	var out := []
	if doc == null:
		return out
	if not sel.is_empty() and not doc.layer_at(sel).is_empty():
		out.append(sel)
	for item in sel_more:
		var p: PackedInt32Array = item as PackedInt32Array
		if p.is_empty() or p == sel or doc.layer_at(p).is_empty():
			continue
		if out.has(p):
			continue
		out.append(p)
	return out


## 整体替换选择。空数组 = 取消选择；最后一个成为主选择。
func set_selection(paths: Array) -> void:
	var valid := []
	for item in paths:
		var p: PackedInt32Array = item as PackedInt32Array
		if p.is_empty() or doc == null or doc.layer_at(p).is_empty():
			continue
		if valid.has(p):
			continue
		valid.append(p)
	if valid.is_empty():
		select_layer_path(PackedInt32Array())
		return
	var primary: PackedInt32Array = valid[valid.size() - 1]
	var more := []
	for p in valid:
		if p != primary:
			more.append(p)
	sel_more = more
	select_layer_path(primary, true)


## 点选时按住 Shift = 加选 / 反选。
func toggle_selection(path: PackedInt32Array) -> void:
	if doc == null:
		return
	if path.is_empty():
		select_layer_path(PackedInt32Array())
		return
	var paths := selected_paths()
	if paths.has(path):
		paths.erase(path)
		set_selection(paths)
		return
	paths.append(path)
	set_selection(paths)


func has_multi_selection() -> bool:
	return selected_paths().size() > 1


# ---------------------------------------------------------------- 剪贴板

func copy_selection() -> void:
	_clipboard = []
	_clipboard3d = []
	if doc == null:
		return
	if doc.is_3d():
		var o := selected_object()
		if not o.is_empty():
			_clipboard3d.append(o.duplicate(true))
		return
	for p in selected_paths():
		var layer: Dictionary = doc.layer_at(p)
		if not layer.is_empty():
			_clipboard.append(layer.duplicate(true))


func has_clipboard() -> bool:
	return not _clipboard.is_empty()


## 粘贴：默认粘进主选择的父级（主选择本身是容器时粘进它里面，跟 Figma/PS 一致）。
## as_sibling = true 时强制"和主选择并列"（原地复制用它 —— 把容器复制进自己里面没意义）。
## offset 不为零时顺带平移（原地复制用它错开一点，免得看不出复制出来了）。
func paste_layers(offset := Vector2.ZERO, as_sibling := false) -> void:
	if doc != null and doc.is_3d():
		_paste_objects()
		return
	if doc == null or _clipboard.is_empty():
		return
	var parent := PackedInt32Array()
	if not sel.is_empty():
		var current := selected_layer()
		if not as_sibling and not current.is_empty() and Layer.is_group(current):
			parent = sel.duplicate()
		else:
			parent = doc.parent_path_of(sel)
	var pasted := []
	for raw in _clipboard:
		if not (raw is Dictionary):
			continue
		var copy: Dictionary = (raw as Dictionary).duplicate(true)
		_reid(copy)
		copy["name"] = String(copy.get("name", "图层")) + " 副本"
		if offset != Vector2.ZERO:
			var t: Dictionary = copy.get("transform", {})
			t["x"] = float(t.get("x", 0.0)) + offset.x
			t["y"] = float(t.get("y", 0.0)) + offset.y
		pasted.append(doc.insert_layer(parent, copy))
	if pasted.is_empty():
		return
	set_selection(pasted)
	apply_change(true, "粘贴图层")


## 原地复制（Ctrl+D）：复制 + 立刻粘贴并错开 24px。
func duplicate_selection() -> void:
	if doc != null and doc.is_3d():
		var o := selected_object()
		if o.is_empty():
			return
		copy_selection()
		_paste_objects(Vector2(1.0, 0.0))
		return
	if selected_paths().is_empty():
		return
	copy_selection()
	paste_layers(Vector2(24.0, 24.0), true)


## 删除所有选中的图层。从深往浅、同层从后往前删，避免下标串位。
func delete_selection() -> void:
	if doc == null:
		return
	if doc.is_3d():
		var o := selected_object()
		if o.is_empty():
			return
		var idx := int(sel[0])
		if idx < 0 or idx >= doc.objects3d.size():
			return
		doc.objects3d.remove_at(idx)
		select_layer_path(PackedInt32Array())
		apply_change(true, "删除对象")
		return
	var paths := selected_paths()
	if paths.is_empty():
		return
	paths.sort_custom(func(a, b) -> bool:
		var pa: PackedInt32Array = a
		var pb: PackedInt32Array = b
		if pa.size() != pb.size():
			return pa.size() > pb.size()
		return pa[pa.size() - 1] > pb[pb.size() - 1])
	for p in paths:
		doc.remove_at(p)
	var parent: PackedInt32Array = doc.parent_path_of(paths[paths.size() - 1])
	var siblings: Array = doc.siblings_at(parent)
	var path := PackedInt32Array()
	if not siblings.is_empty():
		var at := mini(int(paths[paths.size() - 1][paths[paths.size() - 1].size() - 1]),
				siblings.size() - 1)
		path = parent.duplicate()
		path.append(maxi(0, at))
	select_layer_path(path)
	apply_change(true, "删除图层")


## 复制出来的图层必须换 id：节点名、效果层的绑定目标都按 id 找，
## 两份同 id 的图层会让"复制出来的滤镜"作用到原来那层上。
func _reid(layer: Dictionary) -> void:
	var map := {}
	_reid_subtree(layer, map)
	_retarget_subtree(layer, map)


func _reid_subtree(layer: Dictionary, map: Dictionary) -> void:
	var old_id := String(layer.get("id", ""))
	layer["id"] = "l_" + StudioPaths.new_pack_id().substr(0, 8)
	if not old_id.is_empty():
		map[old_id] = String(layer["id"])
	var kids = layer.get("children")
	if kids is Array:
		for child in kids:
			if child is Dictionary:
				_reid_subtree(child, map)


## cover.target 指向被复制进来的一份时，改成新的 id（否则滤镜仍绑在原图层上）。
func _retarget_subtree(layer: Dictionary, map: Dictionary) -> void:
	var cover = layer.get("cover")
	if cover is Dictionary:
		var target := String((cover as Dictionary).get("target", ""))
		if not target.is_empty() and map.has(target):
			(cover as Dictionary)["target"] = String(map[target])
	var kids = layer.get("children")
	if kids is Array:
		for child in kids:
			if child is Dictionary:
				_retarget_subtree(child, map)


func _canvas_changed_selection() -> void:
	if doc != null and doc.is_3d():
		return          # 3D 预览没有选区框
	if _canvas != null and _canvas.has_method("refresh_selection"):
		_canvas.refresh_selection()
	if _inspector != null:
		_inspector.refresh(true)
	if _timeline != null:
		_timeline.refresh()


## 只改了文档元信息（名称）或图层名：不需要重建画布。
func refresh_layers() -> void:
	dirty = true
	if doc != null and doc.is_3d():
		if _objects != null:
			_objects.refresh()
		return
	if _layers != null:
		_layers.refresh()
	if _bar != null:
		_bar.update_status()
	_update_window_title()


func mark_dirty() -> void:
	dirty = true
	if _bar != null:
		_bar.update_status()
	_update_window_title()


func _refresh_all() -> void:
	doc_changed.emit()
	_apply_mode()
	if doc != null and doc.is_3d():
		if _objects != null:
			_objects.refresh()
		if _preview3d != null:
			_preview3d.rebuild()
		if _inspector3d != null:
			_inspector3d.refresh(true)
	else:
		if _layers != null:
			_layers.refresh()
		if _canvas != null:
			_canvas.rebuild()
		if _inspector != null:
			_inspector.refresh(true)
		if _timeline != null:
			_timeline.refresh()
	if _bar != null:
		_bar.update_status()
	_update_window_title()
	_sync_history()


func _update_window_title() -> void:
	var w := get_window()
	if w == null:
		return
	var title := "桌面贴纸 · 壁纸编辑器"
	if doc != null:
		# 还没保存过的工程（没有目录）也要打星号：否则标题看着像"已存盘"。
		var mark := " *" if (dirty or project_dir.is_empty()) else ""
		title = "%s%s — %s" % [String(doc.name), mark, title]
	w.title = title


# ---------------------------------------------------------------- 对话框

func show_message(title: String, text: String) -> void:
	_popup_text(title, text, "好")


func show_error(title: String, text: String) -> void:
	_popup_text(title, text, "知道了")


func show_errors(title: String, problems: PackedStringArray) -> void:
	var lines := PackedStringArray()
	for p in problems:
		lines.append("· " + String(p))
	_popup_text(title, "\n".join(lines), "知道了")


func _popup_text(title: String, text: String, ok_text: String) -> void:
	var d := AcceptDialog.new()
	d.title = title
	d.dialog_text = text
	d.dialog_autowrap = true
	d.ok_button_text = ok_text
	add_child(d)
	d.confirmed.connect(d.queue_free)
	d.canceled.connect(d.queue_free)
	d.close_requested.connect(d.queue_free)
	d.popup_centered(Vector2i(560, 220))


## 有未保存改动时先确认，再执行 then。没有改动就直接执行。
func confirm_discard(then: Callable) -> void:
	if not dirty:
		then.call()
		return
	var d := ConfirmationDialog.new()
	d.title = "未保存的修改"
	d.dialog_text = "当前工程有未保存的修改，继续将丢失这些修改。"
	d.ok_button_text = "继续"
	d.cancel_button_text = "取消"
	add_child(d)
	d.confirmed.connect(func() -> void:
		d.queue_free()
		then.call()
	)
	d.canceled.connect(d.queue_free)
	d.close_requested.connect(d.queue_free)
	d.popup_centered(Vector2i(480, 170))


# ---------------------------------------------------------------- 快捷键 / 关闭

func _shortcut_input(event: InputEvent) -> void:
	if busy or not (event is InputEventKey):
		return
	var key := event as InputEventKey
	if not key.pressed or key.echo:
		return
	# 空格 = 播放/暂停。焦点在文本输入类控件里时不抢（那里空格就是空格）。
	if key.keycode == KEY_SPACE and not key.ctrl_pressed:
		var focus := get_viewport().gui_get_focus_owner()
		if focus is LineEdit or focus is TextEdit:
			return
		toggle_play()
		get_viewport().set_input_as_handled()
		return
	var typing := _focus_is_text()
	if not key.ctrl_pressed:
		# 删除：焦点在输入框里时它是"删字符"，不抢。
		if key.keycode == KEY_DELETE and not typing:
			delete_selection()
			get_viewport().set_input_as_handled()
		return
	match key.keycode:
		KEY_N:
			_bar.popup_new()
			get_viewport().set_input_as_handled()
		KEY_O:
			_bar.popup_open()
			get_viewport().set_input_as_handled()
		KEY_S:
			save_project()
			get_viewport().set_input_as_handled()
		KEY_E:
			_bar.popup_export()
			get_viewport().set_input_as_handled()
		KEY_Z:
			# Ctrl+Shift+Z / Ctrl+Y = 重做，Ctrl+Z = 撤销。
			if key.shift_pressed:
				redo()
			else:
				undo()
			get_viewport().set_input_as_handled()
		KEY_Y:
			redo()
			get_viewport().set_input_as_handled()
		KEY_C:
			if not typing:
				copy_selection()
				get_viewport().set_input_as_handled()
		KEY_V:
			if not typing:
				paste_layers()
				get_viewport().set_input_as_handled()
		KEY_D:
			if not typing:
				duplicate_selection()
				get_viewport().set_input_as_handled()
		KEY_A:
			if not typing:
				_select_all()
				get_viewport().set_input_as_handled()


func _select_all() -> void:
	if doc == null:
		return
	if doc.is_3d():
		# 3D 没有多选编辑，全选等价于"全不选"（避免 Delete 误删一片）。
		select_layer_path(PackedInt32Array())
		return
	var paths := []
	for i in doc.layers.size():
		var p := PackedInt32Array()
		p.append(i)
		paths.append(p)
	set_selection(paths)


## 焦点在文本输入类控件里：Ctrl+C/V、空格、Delete 都该留给输入控件自己处理。
func _focus_is_text() -> bool:
	var focus := get_viewport().gui_get_focus_owner()
	return focus is LineEdit or focus is TextEdit


func _notification(what: int) -> void:
	if what != NOTIFICATION_WM_CLOSE_REQUEST:
		return
	if busy:
		return
	if dirty:
		confirm_discard(func() -> void: get_tree().quit())
	else:
		get_tree().quit()
