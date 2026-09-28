extends Control
## 2D 壁纸起始模板：全屏深色底 + 居中大时钟（时:分:秒，每帧刷新）+ 下方日期。
## 桌面贴纸以内嵌方式运行时整窗鼠标穿透；编辑器 F5 预览时保持可交互。

@onready var _clock: Label = $Clock
@onready var _date: Label = $Date

func _ready() -> void:
	if "--dstk-embedded" in OS.get_cmdline_user_args():
		get_window().mouse_passthrough = true
	_refresh()

func _process(_delta: float) -> void:
	_refresh()

func _refresh() -> void:
	var now := Time.get_datetime_dict_from_system()
	_clock.text = "%02d:%02d:%02d" % [now.hour, now.minute, now.second]
	_date.text = "%d年%d月%d日" % [now.year, now.month, now.day]
