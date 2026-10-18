extends Control
## 界面总装。唯一持有 StudioDoc 的地方；面板只通过它读写文档并发信号。
##
## 与设计 §2.3 一致：本工程不用 class_name（运行库里没有全局类缓存），
## 依赖一律 const + preload；任何被 preload 的脚本里都不要写 static var。
##
## 布局（自上而下）：
##   ProjectBar （36px，新建/打开/保存/导出）
##   HBox       （LayerPanel 240 | CanvasView 自适应 | Inspector 300）

const StudioDoc = preload("res://src/doc/doc.gd")
const StudioPaths = preload("res://src/doc/paths.gd")
const ProjectBar = preload("res://src/ui/project_bar.gd")
const LayerPanel = preload("res://src/ui/layer_panel.gd")
const CanvasView = preload("res://src/ui/canvas_view.gd")
const Inspector = preload("res://src/ui/inspector.gd")

signal doc_changed
signal selection_changed

var doc = null                 # StudioDoc 实例（RefCounted，无类型标注是有意的）
var project_dir := ""          # 用户工程目录（绝对路径）
## 当前选中图层的**路径**（空 = 未选）：[2] = 第 3 个根层，[2, 0] = 它的第一个子层。
var sel := PackedInt32Array()
var dirty := false             # 有没有未保存的改动
var busy := false              # 导出等阻塞操作进行中：禁止新建/打开/退出

var _bar: Control
var _layers: Control
var _canvas: Control
var _inspector: Control


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

	# setup 放在全部节点进树之后：面板的 _ready 只建自己的控件，不依赖 main。
	_bar.setup(self)
	_layers.setup(self)
	_canvas.setup(self)
	_inspector.setup(self)


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
	dirty = false
	_update_window_title()


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


func new_project(title: String, size: Vector2i) -> void:
	if busy:
		return
	var fresh = StudioDoc.create_empty(title, size)
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
func apply_change(rebuild_inspector: bool = false) -> void:
	if doc == null:
		return
	dirty = true
	doc_changed.emit()
	if _canvas != null:
		_canvas.rebuild()
	if _layers != null:
		_layers.refresh()
	if rebuild_inspector and _inspector != null:
		_inspector.refresh(true)
	if _bar != null:
		_bar.update_status()
	_update_window_title()


## 拖动类交互的轻量路径：只改画布上那个节点的位置/尺寸，不重建整棵树
## （每个鼠标移动事件都重造节点树的话，拖起来会一顿一顿的）。
## 松手后走 apply_change(true) 做完整刷新。
func apply_live() -> void:
	if doc == null:
		return
	dirty = true
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
func select_layer_path(path: PackedInt32Array) -> void:
	if doc == null:
		return
	if not path.is_empty() and doc.layer_at(path).is_empty():
		path = PackedInt32Array()
	sel = path
	if _layers != null:
		_layers.sync_selection()
	_canvas_changed_selection()
	selection_changed.emit()


func _canvas_changed_selection() -> void:
	if _canvas != null and _canvas.has_method("refresh_selection"):
		_canvas.refresh_selection()
	if _inspector != null:
		_inspector.refresh(true)


## 只改了文档元信息（名称）或图层名：不需要重建画布。
func refresh_layers() -> void:
	dirty = true
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
	if _layers != null:
		_layers.refresh()
	if _canvas != null:
		_canvas.rebuild()
	if _inspector != null:
		_inspector.refresh(true)
	if _bar != null:
		_bar.update_status()
	_update_window_title()


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
	if not key.pressed or key.echo or not key.ctrl_pressed:
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


func _notification(what: int) -> void:
	if what != NOTIFICATION_WM_CLOSE_REQUEST:
		return
	if busy:
		return
	if dirty:
		confirm_discard(func() -> void: get_tree().quit())
	else:
		get_tree().quit()
