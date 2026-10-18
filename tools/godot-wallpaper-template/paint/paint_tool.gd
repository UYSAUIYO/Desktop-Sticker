extends Node2D
## 壁纸「多效果画笔」设计工具（GPU 画到 SubViewport）。
## 用法：把 target 设成要处理的图片，运行本场景（F6），
##   左键按住拖动 = 在图上刷当前效果；
##   1 / 2 / 3 = 选择效果通道（R 水波 / G 去色 / B 辉光）；
##   E = 切换橡皮；[ / ] = 缩小 / 放大笔刷；- / = = 降低 / 提高强度；
##   S = 把遮罩保存为 save_path 指定的 PNG。
## 保存出的 mask 图（RGB=三通道覆盖度）交给 wallpaper_effects.gdshader，
## 壁纸主场景的 Sprite2D 用同一个着色器引用它即可，运行时无需本工具。

@export var target: Texture2D
@export_file("*.png") var save_path: String = "res://wallpaper_mask.png"
@export var mask_size_override: Vector2i = Vector2i(0, 0)   # 0 = 用 target 尺寸
@export var brush_radius: float = 60.0
@export_range(0.0, 1.0) var hardness: float = 0.5
@export_range(0.05, 1.0) var strength: float = 0.35

var _vp: SubViewport
var _stamp: Sprite2D
var _preview: Sprite2D
var _cursor: Sprite2D
var _hud: Label
var _dot: ImageTexture

var _channel := 0            # 0=R 1=G 2=B
var _erasing := false
var _painting := false
var _add_mat: CanvasItemMaterial
var _sub_mat: CanvasItemMaterial
var _effect_mat: ShaderMaterial

const _CHANNEL_COLORS := [Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)]
const _DOT_SIZE := 128


func _ready() -> void:
	_build()
	_refresh_all()


func _build() -> void:
	_add_mat = CanvasItemMaterial.new()
	_add_mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	_sub_mat = CanvasItemMaterial.new()
	_sub_mat.blend_mode = CanvasItemMaterial.BLEND_MODE_SUB

	# 遮罩画布：透明底 + 从不清屏（笔印累积）
	_vp = SubViewport.new()
	_vp.size = _mask_size()
	_vp.transparent_bg = true
	_vp.render_target_clear_mode = SubViewport.CLEAR_MODE_NEVER
	_vp.render_target_update_mode = SubViewport.UPDATE_ONCE
	add_child(_vp)

	_stamp = Sprite2D.new()
	_stamp.centered = true
	_stamp.texture = _make_dot()
	_stamp.material = _add_mat
	_vp.add_child(_stamp)

	# 预览：目标图 + 多效果着色器，mask 用累积出来的视口纹理
	_effect_mat = ShaderMaterial.new()
	_effect_mat.shader = preload("res://paint/wallpaper_effects.gdshader")
	_preview = Sprite2D.new()
	_preview.centered = true
	_preview.texture = target
	_preview.material = _effect_mat
	add_child(_preview)

	_cursor = Sprite2D.new()
	_cursor.centered = true
	_cursor.texture = _stamp.texture
	_cursor.z_index = 10
	add_child(_cursor)

	_hud = Label.new()
	_hud.position = Vector2(-_vp.size.x * 0.5 + 12, -_vp.size.y * 0.5 + 8)
	_hud.z_index = 11
	add_child(_hud)


func _mask_size() -> Vector2i:
	if mask_size_override.x > 0 and mask_size_override.y > 0:
		return mask_size_override
	if target != null:
		return Vector2i(target.get_width(), target.get_height())
	return Vector2i(1024, 1024)


func _make_dot() -> ImageTexture:
	var img := Image.create(_DOT_SIZE, _DOT_SIZE, false, Image.FORMAT_RGBA8)
	var c := (_DOT_SIZE - 1) * 0.5
	var rr := maxf(_DOT_SIZE * 0.5, 1.0)
	for y in _DOT_SIZE:
		for x in _DOT_SIZE:
			var d := Vector2(x - c, y - c).length() / rr
			var a := clampf(1.0 - (d - hardness) / maxf(1.0 - hardness, 0.001), 0.0, 1.0)
			img.set_pixel(x, y, Color(1, 1, 1, a))
	return ImageTexture.create_from_image(img)


func _refresh_all() -> void:
	if _vp == null:
		return
	_vp.size = _mask_size()
	_dot = _make_dot()
	_stamp.texture = _dot
	_cursor.texture = _dot
	var s := (brush_radius * 2.0) / float(_DOT_SIZE)
	_stamp.scale = Vector2(s, s)
	_cursor.scale = Vector2(s, s)
	_preview.texture = target
	_effect_mat.set_shader_parameter("mask", _vp.get_texture())
	_update_hud()


func _update_hud() -> void:
	if _hud == null:
		return
	var names := ["水波(R)", "去色(G)", "辉光(B)"]
	var mode := "画笔" if not _erasing else "橡皮"
	_hud.text = "效果:%s  模式:%s  半径:%d  硬度:%.2f  强度:%.2f\n1/2/3 切换 · E 橡皮 · [ ] 半径 · - = 强度 · S 保存" % [
		names[_channel], mode, int(brush_radius), hardness, strength
	]


func _stamp_color() -> Color:
	return Color(_CHANNEL_COLORS[_channel], strength)


func _do_stamp(pos: Vector2) -> void:
	_stamp.position = pos
	_stamp.self_modulate = _stamp_color()
	_stamp.material = _sub_mat if _erasing else _add_mat
	# 只渲染一帧，笔印累积到不清屏的遮罩上
	_vp.render_target_update_mode = SubViewport.UPDATE_ONCE


func _mouse_to_mask_px() -> Vector2:
	# 目标图居中；to_local 已含节点变换；映射到 0..1 再乘遮罩尺寸
	if _preview.texture == null:
		return Vector2.ZERO
	var local := _preview.to_local(get_global_mouse_position())
	var uv := local / _preview.texture.get_size() + 0.5
	return Vector2(uv.x * _vp.size.x, uv.y * _vp.size.y)


func _unhandled_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT:
		_painting = e.pressed
		if e.pressed:
			_do_stamp(_mouse_to_mask_px())
	elif e is InputEventMouseMotion and _painting:
		_do_stamp(_mouse_to_mask_px())
	elif e is InputEventKey and e.pressed and not e.echo:
		_on_key(e.keycode)


func _process(_dt: float) -> void:
	if _cursor != null:
		_cursor.position = _preview.to_local(get_global_mouse_position())
		_cursor.self_modulate = _stamp_color()


func _on_key(k: int) -> void:
	match k:
		KEY_1: _channel = 0
		KEY_2: _channel = 1
		KEY_3: _channel = 2
		KEY_E: _erasing = not _erasing
		KEY_BRACKETRIGHT: brush_radius = clampf(brush_radius + 5.0, 4.0, 512.0)
		KEY_BRACKETLEFT: brush_radius = clampf(brush_radius - 5.0, 4.0, 512.0)
		KEY_EQUAL: strength = clampf(strength + 0.05, 0.05, 1.0)
		KEY_MINUS: strength = clampf(strength - 0.05, 0.05, 1.0)
		KEY_S: _save_mask()
		_: return
	_refresh_all()


func _save_mask() -> void:
	var img := _vp.get_texture().get_image()
	if img == null:
		return
	var err := img.save_png(save_path)
	if _hud != null:
		_hud.text = "已保存遮罩 → %s（err=%d）" % [save_path, err]
