extends SceneTree

# verify_trinity.gd
# Automated verification of the "Absolute Trinity" terrain fixes.

func _init():
	var main = load("res://main.gd").new()
	root.add_child(main)
	
	# Wait for everything to initialize
	await create_timer(3.0).timeout
	main.set_process(false) # Stop main camera logic from overriding
	
	var rig = main.get_node_or_null("CameraRig")
	if not rig:
		push_error("[VERIFY] CameraRig not found!")
		quit()
		return
		
	var camera = main.get_node("CameraRig/Pitch/MainCamera")
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.current = true
	
	var pitch = rig.get_node("Pitch")
	
	# 1. City Floor Alignment (Lorencia 130, 130) - Absolute Top Down
	rig.position = Vector3(130, 0, 255 - 130)
	pitch.rotation_degrees.x = -90
	camera.size = 20.0
	await create_timer(1.0).timeout
	await _save_capture(main, "parity_city_floor.png")
	
	# 2. Rock Mirroring (Lorencia 100, 100) - Absolute Top Down
	rig.position = Vector3(100, 0, 255 - 100)
	pitch.rotation_degrees.x = -90
	camera.size = 50.0
	await create_timer(1.0).timeout
	await _save_capture(main, "parity_rock_mirror.png")
	
	# 3. Grass Tiling (Lorencia 50, 50)
	rig.position = Vector3(50, 0, 255 - 50)
	pitch.rotation_degrees.x = -90
	camera.size = 80.0
	await create_timer(1.0).timeout
	await _save_capture(main, "parity_grass_tiling.png")
	
	print("[VERIFY] All screenshots saved.")
	quit()

func _save_capture(main: Node, fname: String) -> void:
	# Give the GPU a few frames to finish rendering the new camera view
	for i in range(10):
		await main.get_viewport().get_tree().process_frame

	var texture = main.get_viewport().get_texture()
	if not texture:
		push_error("[VERIFY] Viewport texture is NULL for " + fname)
		return

	var img = texture.get_image()
	if not img:
		push_error("[VERIFY] Image is NULL for " + fname)
		return

	# Copy to desktop for easy viewing by AI
	var desktop_path = "/Users/karlisfeldmanis/Desktop/mu_remaster/" + fname
	var err = img.save_png(desktop_path)
	if err == OK:
		print("[VERIFY] Captured: ", fname)
	else:
		push_error("[VERIFY] Failed to save " + fname + " - Error: " + str(err))
