@tool
extends Control
## 「壁纸」停靠面板：把校验 / 导出 / 元数据 / 权限声明 / 热应用收进一个面板，
## 让 Godot 编辑器用起来更聚焦于壁纸创作。纯 UI —— 字段读写走 get_fields/set_fields，
## 具体校验与导出逻辑在 plugin.gd 里。

signal validate_requested
signal export_requested(apply: bool)
signal fields_changed
signal template_requested(dimension: String)

const PERMISSIONS := [
	["network", "网络访问"],
	["file_outside", "包外文件读写"],
	["exec", "执行外部程序"],
	["system", "系统访问"],
]

var _title: LineEdit
var _author: LineEdit
var _description: LineEdit
var _tags: LineEdit
var _categories: LineEdit
var _template_option: OptionButton
var _permission_boxes: Dictionary = {}   # id -> CheckBox
var _problems: Label
var _status: Label

func _init() -> void:
	custom_minimum_size = Vector2(320, 420)
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL

	var scroll := ScrollContainer.new()
	scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(scroll)

	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_theme_constant_override("separation", 8)
	scroll.add_child(box)

	box.add_child(_section("起始模板"))
	var tpl_row := HBoxContainer.new()
	tpl_row.add_theme_constant_override("separation", 8)
	box.add_child(tpl_row)
	_template_option = OptionButton.new()
	_template_option.add_item("2D 壁纸", 0)
	_template_option.add_item("3D 壁纸", 1)
	_template_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tpl_row.add_child(_template_option)
	var load_tpl := Button.new()
	load_tpl.text = "载入为主场景"
	load_tpl.tooltip_text = "把选定的起始场景设为工程主场景并打开编辑（会覆盖当前主场景）"
	load_tpl.pressed.connect(func(): template_requested.emit("2d" if _template_option.selected == 0 else "3d"))
	tpl_row.add_child(load_tpl)

	box.add_child(_section("项目信息"))
	_title = _field(box, "标题")
	_author = _field(box, "作者")
	_description = _field(box, "简介")
	_description.text_change_causes_revert = false
	_tags = _field(box, "标签（逗号分隔）")
	_categories = _field(box, "分类（逗号分隔）")

	box.add_child(_section("能力声明（导出时写入清单，桌面贴纸仅展示不强制）"))
	for entry in PERMISSIONS:
		var cb := CheckBox.new()
		cb.text = entry[1]
		cb.tooltip_text = String(entry[0])
		cb.set_meta("id", entry[0])
		cb.toggled.connect(func(_v): fields_changed.emit())
		box.add_child(cb)
		_permission_boxes[entry[0]] = cb

	box.add_child(_section("操作"))
	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 8)
	box.add_child(actions)

	var validate := Button.new()
	validate.text = "校验场景"
	validate.pressed.connect(func(): validate_requested.emit())
	actions.add_child(validate)

	var export := Button.new()
	export.text = "导出 .dswall"
	export.pressed.connect(func(): export_requested.emit(false))
	actions.add_child(export)

	var apply := Button.new()
	apply.text = "导出并应用到桌面"
	apply.tooltip_text = "导出后投递到桌面贴纸收件箱，运行中的实例会立即换上这个壁纸"
	apply.pressed.connect(func(): export_requested.emit(true))
	actions.add_child(apply)

	_problems = Label.new()
	_problems.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_problems.text = "（尚未校验）"
	box.add_child(_section("校验结果"))
	box.add_child(_problems)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.text = ""
	box.add_child(_status)

	for ctl in [_title, _author, _description, _tags, _categories]:
		ctl.text_changed.connect(func(_t): fields_changed.emit())

# --- 字段读写 ---

func set_fields(cfg: Dictionary) -> void:
	_title.text = String(cfg.get("title", ""))
	_author.text = String(cfg.get("author", ""))
	_description.text = String(cfg.get("description", ""))
	_tags.text = ", ".join(PackedStringArray(cfg.get("tags", [])))
	_categories.text = ", ".join(PackedStringArray(cfg.get("categories", [])))
	_template_option.selected = 0 if String(cfg.get("dimension", "3d")) == "2d" else 1
	var perms: Array = cfg.get("requested_permissions", [])
	for id in _permission_boxes.keys():
		(_permission_boxes[id] as CheckBox).set_pressed_no_signal(id in perms)

func get_fields() -> Dictionary:
	return {
		"title": _title.text.strip_edges(),
		"author": _author.text.strip_edges(),
		"description": _description.text.strip_edges(),
		"tags": _split_list(_tags.text),
		"categories": _split_list(_categories.text),
		"requested_permissions": _checked_permissions(),
	}

func set_problems(problems: PackedStringArray) -> void:
	if problems.is_empty():
		_problems.text = "✓ 校验通过：场景可以导出为壁纸包。"
	else:
		var lines := PackedStringArray()
		for p in problems:
			lines.append("• " + String(p))
		_problems.text = "\n".join(lines)

func set_status(text: String) -> void:
	_status.text = text

# --- 小工具 ---

func _checked_permissions() -> PackedStringArray:
	var out := PackedStringArray()
	for id in _permission_boxes.keys():
		if (_permission_boxes[id] as CheckBox).button_pressed:
			out.append(String(id))
	return out

func _split_list(text: String) -> PackedStringArray:
	var out := PackedStringArray()
	for piece in text.split(",", false):
		var s := String(piece).strip_edges()
		if not s.is_empty():
			out.append(s)
	return out

func _section(title: String) -> Label:
	var l := Label.new()
	l.text = title
	l.add_theme_font_size_override("font_size", 13)
	l.modulate = Color(1, 1, 1, 0.7)
	return l

func _field(parent: Control, label_text: String) -> LineEdit:
	parent.add_child(_label(label_text))
	var edit := LineEdit.new()
	edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(edit)
	return edit

func _label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l
