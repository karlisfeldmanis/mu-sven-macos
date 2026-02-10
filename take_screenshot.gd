extends Node

func _ready():
	await get_tree().create_timer(2.0).timeout
	print("[Screenshot] Capturing screenshot...")
	var img = get_viewport().get_texture().get_image()
	img.save_png("main_screenshot.png")
	print("[Screenshot] Screenshot saved to main_screenshot.png")
	await get_tree().create_timer(0.5).timeout
	get_tree().quit()
