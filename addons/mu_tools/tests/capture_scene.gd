@tool
extends Node3D

# Single-capture validation scene for object rotations.
# Renders the city center and saves a screenshot for visual comparison.

func _ready():
	print("\n=== ROTATION VALIDATION ===")
	print("Using matrix-based similarity transform (W·M·Wᵀ)")
	
	var window = get_window()
	window.mode = Window.MODE_WINDOWED
	window.size = Vector2i(1024, 1024)
	window.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	
	# Setup scene
	var sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 45, 0)
	add_child(sun)
	
	var camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 40.0
	camera.position = Vector3(128, 200, 128)
	camera.rotation_degrees = Vector3(-90, 0, 0)
	add_child(camera)
	
	# City center filter (MU coords ~130,130 ±25 tiles)
	var mu_cx = 130
	var mu_cy = 130
	var half = 25
	var filter = Rect2(mu_cy - half, mu_cx - half, half * 2, half * 2)
	
	var mu_world = load("res://addons/mu_tools/nodes/mu_world.gd").new()
	mu_world.world_id = 0
	mu_world.show_objects = true
	mu_world.filter_rect = filter
	add_child(mu_world)
	
	# Wait for load and render
	for f in range(120):
		await get_tree().process_frame
	
	# Capture
	var img = get_viewport().get_texture().get_image()
	if img and not img.is_empty():
		var w = img.get_width()
		var h = img.get_height()
		var size = min(w, h, 1024)
		var cx = w / 2
		var cy = h / 2
		img = img.get_region(Rect2i(cx - size/2, cy - size/2, size, size))
		img.save_png("rotation_validation.png")
		print("✓ Saved: rotation_validation.png")
	
	get_tree().quit()
