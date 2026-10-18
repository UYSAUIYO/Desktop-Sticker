extends Control

var t0 := 0

func _ready() -> void:
	t0 = Time.get_ticks_msec()
	print("PROBE READY size=", get_viewport_rect().size)

func _input(event: InputEvent) -> void:
	var t := Time.get_ticks_msec() - t0
	if event is InputEventMouseButton:
		print("PROBE t=", t, " BUTTON index=", event.button_index, " pressed=", event.pressed, \
			" pos=", event.position, " mask=", event.button_mask)
	elif event is InputEventMouseMotion:
		print("PROBE t=", t, " MOVE pos=", event.position, " relative=", event.relative, \
			" mask=", event.button_mask)