extends Control

func _ready() -> void:
	print("PROBE READY size=", get_viewport_rect().size)

func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		print("PROBE BUTTON index=", event.button_index, " pressed=", event.pressed, " pos=", event.position)
	elif event is InputEventMouseMotion:
		print("PROBE MOVE pos=", event.position)