extends Node3D
## 壁纸模板主脚本：整窗鼠标穿透 + 多对象差速旋转。
## GodotBackend 会把本项目的窗口嵌入桌面 WorkerW 层（图标之下），
## 穿透让桌面点击（图标、双击空白）落到正常的目标上。

@export var hero_spin := 0.5      # 弧度/秒（主球自转）
@export var ring_spin := -0.9     # 环反向旋转
@export var moon_orbit := 0.6     # 小球绕轨道角速度

@onready var hero: MeshInstance3D = $Hero
@onready var ring: MeshInstance3D = $Ring
@onready var moon: MeshInstance3D = $Moon

func _ready() -> void:
	# 只有被壁纸模块以内嵌方式启动时才需要整窗鼠标穿透（"--dstk-embedded" 在 `--` 之后的用户参数里）；
	# 在编辑器里 F5 预览时要能正常交互（旋转/点击），所以不能无条件开穿透。
	# 注：Windows 上用 FLAG_MOUSE_PASSTHROUGH（HTTRANSPARENT），不能用退化多边形——
	# 后者是用 SetWindowRgn 把窗口形状设成多边形，退化多边形会把窗口裁到几像素（实测）。
	if "--dstk-embedded" in OS.get_cmdline_user_args():
		get_window().mouse_passthrough = true

func _process(delta: float) -> void:
	hero.rotate_y(hero_spin * delta)
	ring.rotate_x(ring_spin * delta)
	moon.position = Vector3(cos(moon_orbit * Time.get_ticks_msec() / 1000.0) * 3.3,
			0.9 + 0.3 * sin(Time.get_ticks_msec() / 1000.0 * 0.8),
			sin(moon_orbit * Time.get_ticks_msec() / 1000.0) * 3.3)
