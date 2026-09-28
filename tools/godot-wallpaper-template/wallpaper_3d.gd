extends Node3D
## 3D 壁纸起始模板：中心发光球 + 三颗卫星球环绕 + 一块始终朝向相机的文字（显示当前时间）。
## 桌面贴纸以内嵌方式运行时整窗鼠标穿透；编辑器 F5 预览时保持可交互。

@export var orbit_radius := 3.0        # 卫星环绕半径
@export var orbit_speed := 0.6         # 环绕角速度（弧度/秒）
@export var show_time := true          # 中心文字是否显示当前时间，否则显示 title_text
@export var title_text := "Desktop Sticker"

@onready var _title: Label3D = $Title
@onready var _core: MeshInstance3D = $Core
@onready var _orbiters := [$Orbiter1, $Orbiter2, $Orbiter3]

func _ready() -> void:
	if "--dstk-embedded" in OS.get_cmdline_user_args():
		get_window().mouse_passthrough = true

func _process(delta: float) -> void:
	var t := Time.get_ticks_msec() / 1000.0
	_core.rotate_y(0.3 * delta)
	var n := maxi(1, _orbiters.size())
	for i in _orbiters.size():
		var angle := t * orbit_speed + i * (TAU / n)
		(_orbiters[i] as MeshInstance3D).position = Vector3(
			cos(angle) * orbit_radius, sin(angle * 0.8) * 0.7, sin(angle) * orbit_radius)
		(_orbiters[i] as MeshInstance3D).rotate_y(1.5 * delta)
	if _title:
		_title.text = Time.get_time_string_from_system() if show_time else title_text
