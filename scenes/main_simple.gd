extends Node3D

const MUTerrainSimple = preload("res://addons/mu_tools/mu_terrain_simple.gd")
const MUStateMachine = preload("res://addons/mu_tools/mu_state_machine.gd") 
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")
const MUBMDRegistry = preload("res://addons/mu_tools/mu_bmd_registry.gd")

var _terrain: MUTerrainSimple
var _character: Node3D
var _camera_rig: Node3D
var _camera: Camera3D
var _state_machine: MUStateMachine
# Debug Logic
var _is_debug_cam: bool = true 
var _debug_cam_pos: Vector3 = Vector3(128.0, 1.5, 128.0) 
var _debug_ui: CanvasLayer 
var _debug_label: Label

# Camera Settings
var _yaw: float = 0.0 # Match minimap (North = -Z)
var _pitch: float = -90.0 # DEBUG TOP-DOWN: Locked for alignment verification
var _zoom: float = 150.0 
var _camera_offset: Vector3 = Vector3(0.0, 0.0, 0.0) 

# Persistence
var _save_timer: float = 0.0
var _last_saved_pos: Vector3 = Vector3.ZERO
const SAVE_INTERVAL: float = 2.0
const SAVE_PATH: String = "user://savegame.json"
const SCREENSHOT_DIR: String = "user://screenshots"

func _ready():
	print("[MainSimple] Starting simplified engine...")
	
	# 1. Setup Lighting
	_setup_lighting()
	_setup_environment()
	
	_setup_camera()
	_load_terrain()
	_spawn_character()
	
	if OS.get_environment("CAPTURE_SCREENSHOT") == "1":
		print("[MainSimple] Fountain Out-of-View capture requested.")
		# 1. Disable process follow
		set_process(false)
		
		# 2. Position Rig so Fountain (128, 128) is just OFF bottom edge (Yaw 180)
		# Moving Rig "North" (lower Z) to push high-Z fountain off the bottom.
		var target_pos = Vector3(128.0, 1.65, 90.0) 
		var fov_val = 10.0
		_yaw = 180.0
		_pitch = -90.0
		_zoom = 180.0 
		
		_camera_rig.position = target_pos
		_camera_rig.rotation_degrees.y = _yaw
		_camera_rig.get_node("Pitch").rotation_degrees.x = _pitch
		_camera.fov = fov_val
		_camera.position.z = _zoom
		
		await get_tree().create_timer(3.0).timeout
		_take_screenshot("fountain_off_edge")
		get_tree().quit()
	
	# Restore State
	var saved_data = _load_state()
	if not saved_data.is_empty():
		if saved_data.has("cam_x") and saved_data.has("cam_z"):
			_debug_cam_pos = Vector3(saved_data["cam_x"], 1.5, saved_data["cam_z"])
			_camera_rig.position = _debug_cam_pos
		if saved_data.has("cam_zoom"):
			_zoom = saved_data["cam_zoom"]
			_camera.position.z = _zoom
		if saved_data.has("cam_yaw"):
			_yaw = saved_data["cam_yaw"]
			_camera_rig.rotation_degrees.y = _yaw
		if saved_data.has("player_x") and _character:
			_character.position = Vector3(saved_data["player_x"], saved_data["player_y"], saved_data["player_z"])
			_last_saved_pos = _character.position
	
	_setup_debug_ui()
	print("[MainSimple] Ready. Tree Nodes: ", get_tree().get_node_count())
	for child in get_children():
		print(" - ", child.name, " (", child.get_class(), ")")

func _setup_lighting():
	# Clean up any existing lights
	for child in get_children():
		if child is DirectionalLight3D:
			child.queue_free()
	
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	
	# Aligning sun with camera rig (45 deg yaw, -48.5 deg pitch)
	sun.rotation_degrees = Vector3(-60, 45 + 15, 0) 
	sun.light_energy = 1.0
	sun.light_indirect_energy = 0.5
	sun.shadow_enabled = true
	
	# OPTIMIZED SHADOWS (now that winding is fixed)
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 40.0 # Small distance = high res shadows
	
	# Normal bias can be much lower now that triangles face Up
	sun.shadow_bias = 0.02
	sun.shadow_normal_bias = 1.0
	sun.shadow_blur = 1.0 # Sharp, clean shadows
	
	add_child(sun)
	
	# _setup_environment() # MUTerrainSimple already does this with Sven-Parity logic 
	

func _setup_environment() -> void:
	# Clean up existing environment
	for child in get_children():
		if child is WorldEnvironment:
			child.queue_free()
			
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.1, 0.1, 0.12) # Dark grey-blue instead of pitch black
	
	# Ambient Lighting: This will lighten up the dark sides of buildings
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.3, 0.3, 0.35) # Soft cool ambient
	env.ambient_light_energy = 0.8
	
	# Tonemapping for better color range
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.ssr_enabled = false # Not needed
	env.ssao_enabled = true # Adds depth to corners
	env.ssil_enabled = false
	env.sdfgi_enabled = false
	
	var world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)
	print("[MainSimple] Environment updated with ambient light and SSAO")

func _setup_camera():
	_camera_rig = Node3D.new()
	_camera_rig.name = "CameraRig"
	add_child(_camera_rig)
	
	var pitch_node = Node3D.new()
	pitch_node.name = "Pitch"
	_camera_rig.add_child(pitch_node)
	
	_camera = Camera3D.new()
	_camera.name = "MainCamera"
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.fov = 10.0 
	_camera.current = true 
	_camera.position.z = 25.0 
	_camera.near = 0.5
	_camera.far = 2000.0 
	
	pitch_node.add_child(_camera)
	
	_camera_rig.rotation_degrees.y = _yaw
	pitch_node.rotation_degrees.x = _pitch
	
	_is_debug_cam = true # Default to manual control for this phase
	_debug_cam_pos = _camera_rig.position
	
	print("[MainSimple] Camera Setup: Pitch=%.1f, Yaw=%.1f, Zoom=%.1f" % [_pitch, _yaw, _zoom])

func _setup_debug_ui():
	_debug_ui = CanvasLayer.new()
	add_child(_debug_ui)
	
	_debug_label = Label.new()
	_debug_label.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT, Control.PRESET_MODE_MINSIZE, 20)
	_debug_label.add_theme_font_size_override("font_size", 24)
	_debug_label.add_theme_color_override("font_color", Color.YELLOW)
	_debug_ui.add_child(_debug_label)
	print("[MainSimple] Debug UI initialized.")

func _load_terrain():
	_terrain = MUTerrainSimple.new()
	_terrain.name = "Terrain"
	_terrain.world_id = 0
	# Point to reference data
	_terrain.data_path = "res://reference/MuMain/src/bin/Data" 
	add_child(_terrain)

func _spawn_character():
	# Use existing character loader logic or simplified
	# We need a character scene or just a mesh.
	# Let's try to reuse `main.gd`'s `load_character_with_state_machine` logic but simplified
	
	# Initial Spawn (Matching Donkey 128.2, 118.7)
	# (Moved to Mirror-X calculation below)
	
	# Create character
	_character = Node3D.new()
	_character.name = "Player"
	_character.visible = true
	add_child(_character)
	_character.add_to_group("player_character")
	
	# Load Skeleton from player.bmd (Using Registry)
	var bmd_path = "res://raw_data/Player/player.bmd"
	var parser = MUBMDRegistry.get_bmd(bmd_path)
	
	if parser:
		# We need a Skeleton3D. BMDParser usually doesn't return a skeleton directly suitable for Node3D tree easily
		# without the MUMeshBuilder or similar.
		# Let's use the SkeletonBuilder if available?
		var MUSkeletonBuilder = preload("res://addons/mu_tools/skeleton_builder.gd")
		# Fix: Pass arrays, not parser object directly
		var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions) 
		if skeleton:
			# Match legacy structure EXACTLY:
			# Character -> MUContainer -> [Skeleton, AnimationPlayer]
			var mu_container = Node3D.new()
			mu_container.name = "MUContainer"
			mu_container.rotation_degrees.x = 0.0
			
			# FIX: Sane offset to center mesh visually
			# Visual: Red dot is "Up-Right" (North-East).
			# Moving Mesh North-East (World +X, -Z).
			# Reduced from 5.0 to 1.2 to keep on screen.
			# Reset visual offset to zero to prevent rotation swing
			mu_container.position = Vector3.ZERO 
			
			_character.add_child(mu_container)
			
			
			skeleton.name = "Skeleton" 
			mu_container.add_child(skeleton)
			# (Reflection removed)
			
			# Ensure meshes cast shadows
			for child in skeleton.get_children():
				if child is MeshInstance3D:
					child.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
			
			# Setup AnimationPlayer (Sibling in MUContainer)
			var anim_player = AnimationPlayer.new()
			anim_player.name = "AnimationPlayer"
			mu_container.add_child(anim_player) 
			
			var AnimationBuilder = preload("res://addons/mu_tools/animation_builder.gd")
			var library = AnimationBuilder.build_animation_library(bmd_path, parser.actions, skeleton)
			anim_player.add_animation_library("", library)
			print("[MainSimple] Built AnimationLibrary with %d animations" % library.get_animation_list().size())
			# Attach parts?
			_attach_part(skeleton, "res://raw_data/Player/ArmorClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/PantClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/HelmClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/GloveClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/BootClass01.bmd")
			
			# Setup State Machine
			_state_machine = MUStateMachine.new()
			_state_machine.name = "StateMachine"
			_state_machine.weapon_class = "Sword" # Set weapon class
			add_child(_state_machine)
			
			# Wait for terrain to be ready if needed (usually _ready runs fast, but safely..)
			# Ensure terrain has heightmap loaded (it loads in _ready which is good)
			_state_machine.setup(anim_player, bmd_path)
			_state_machine.set_terrain(_terrain)
			_state_machine.set_state(MUStateMachine.State.IDLE)
			_state_machine.update_animation() # Force initial animation
			
			# Position (Standard Mapping 1:1)
			# Position (Mirror-X Mapping)
			# Tavern Body (Type 115) at MU (143, 147)
			# Position should be correct now with Mirror-X basis conversion
			var mu_x_local = 143.0
			var mu_y_local = 147.0
			var spawn_tile_x = 255.0 - mu_x_local
			var spawn_tile_z = mu_y_local
			
			var h = _get_height_at(spawn_tile_x, spawn_tile_z)
			print("[MainSimple] Sampled Spawn Height: ", h)
			# Universal Math: Restoring Stable Baseline (Scale X = -1).
			_character.position = Vector3(
				spawn_tile_x, 
				h, 
				spawn_tile_z
			)
			# Revert to Scale X = -1 (Horizontal Flip) to match terrain.
			# But rotate 180 degrees around Y to correct "facing opposite"
			_character.rotate_y(PI)
			_character.scale.x = -1.0
			
			print("[MainSimple] DEBUG: Spawned at: ", _character.position)
			
			_last_saved_pos = _character.position
			
			# Explicitly update state machine pos
			_state_machine.snap_to_terrain(_character)
			_state_machine.target_position = _character.position
			_state_machine.is_moving = false # STOP MOVEMENT
			
			# Force camera to snap immediately
			_camera_rig.position = _character.position
			_camera_rig.position.y += 1.5
	else:
		print("[MainSimple] Failed to load player BMD")

func _attach_part(skeleton, path):
	# Simplified attachment reuse
	var MUMB = preload("res://addons/mu_tools/mesh_builder.gd")
	var p = MUBMDRegistry.get_bmd(path)
	if p:
		for mesh_idx in range(p.get_mesh_count()):
			var b_mesh = p.get_mesh(mesh_idx)
			var mi = MUMB.create_mesh_instance(b_mesh, skeleton, path, p, true)
			if mi:
				skeleton.add_child(mi)

func _get_height_at(world_x: float, world_z: float) -> float:
	if not _terrain or not _terrain.heightmap: return 0.0
	
	var x = world_x
	var z = world_z
	
	# Clamp to terrain bounds
	var fx = floor(x)
	var fz = floor(z)
	
	# Mirror-X Mapping:
	# Godot X = 255 - MU Column, Godot Z = MU Row
	var mu_col = int(255.0 - x)
	var mu_row = int(z) 
	
	# Clamp mu_row/mu_col
	mu_col = clampi(mu_col, 0, 255)
	mu_row = clampi(mu_row, 0, 255)
	
	# Get fractional part within the cell [0,1]
	var dx = x - fx
	var dz = z - fz
	
	# Get heights of the 4 corners: index = Row * 256 + Col
	var h00 = _get_raw_height(mu_row, mu_col)
	var h10 = _get_raw_height(mu_row, mu_col + 1)
	var h01 = _get_raw_height(mu_row + 1, mu_col)
	var h11 = _get_raw_height(mu_row + 1, mu_col + 1)
	
	# Bilinear interpolation
	# Along X-axis (Columns)
	var top = h00 + (h10 - h00) * dx
	var bottom = h01 + (h11 - h01) * dx
	
	# Along Z-axis (Rows)
	return (top + (bottom - top) * dz)
func _get_raw_height(row: int, col: int) -> float:
	var idx = row * 256 + col # Row-Major index
	if idx >= 0 and idx < _terrain.heightmap.size():
		return _terrain.heightmap[idx]
	return 0.0

func _process(delta):
	if not _character or not _camera_rig: return
	
	if _character:
		# Persistence: Autosave
		_save_timer += delta
		if _save_timer >= SAVE_INTERVAL:
			_save_timer = 0.0
			# Save if character OR debug camera moved
			if _character.position.distance_to(_last_saved_pos) > 0.1 or _camera_rig.position.distance_to(_debug_cam_pos) > 0.1:
				_save_state()
				
		# Follow Cam (Disabled in Manual Debug)
		if not _is_debug_cam:
			var t = _character.position
			t.y += 1.5
			_camera_rig.position = t + _camera_offset
		else:
			_camera_rig.position = _debug_cam_pos

		if _debug_label:
			_debug_label.text = "MU Pos: (%.1f, %.1f)\nZoom: %.1f\nYaw: %.1f\nWASD: Move | Q/E: Rotate | F: 90° Step" % [
				_debug_cam_pos.x, _debug_cam_pos.z, _zoom, fmod(_yaw, 360.0)
			]

	# AUTO ORBIT REMOVED
	
	if Engine.get_frames_drawn() % 60 == 0:
		_camera.make_current() # FORCE
		print("[MainSimple] Player Pos: ", _character.global_position, " Rig Pos: ", _camera_rig.global_position)
	
	# Movement Logic
	if _state_machine and _state_machine.is_moving:
		print("Moving to: ", _state_machine.target_position)
		var move_speed = 6.0 # Base run/walk speed
		var dir = _state_machine.target_position - _character.position
		dir.y = 0 # Plane movement
		
		var dist = dir.length()
		if dist > 0.1:
			dir = dir.normalized()
			# Move
			_character.position += dir * move_speed * delta
			
			# Face target
			var target_look = _character.position + dir
			_character.look_at(Vector3(target_look.x, _character.position.y, target_look.z), Vector3.UP)
			
			# Snap to terrain height
			var h = _get_height_at(_character.position.x, _character.position.z)
			_character.position.y = h
		else:
			# Reached target
			_state_machine.is_moving = false
			_state_machine.set_state(MUStateMachine.State.IDLE)
			_character.position = _state_machine.target_position # Snap to exact
			# Snap height
			var h = _get_height_at(_character.position.x, _character.position.z)
			_character.position.y = h
	
	_handle_input(delta)

func _unhandled_input(event: InputEvent):
	if event is InputEventMouseButton:
		var zoom_step = 25.0
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_zoom = max(10.0, _zoom - zoom_step)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_zoom = min(1500.0, _zoom + zoom_step)
		_camera.position.z = _zoom
	
	if event is InputEventKey and event.pressed:
		var zoom_step = 25.0
		if event.keycode == KEY_F:
			_yaw = fmod(_yaw + 90.0, 360.0)
			print("[MainSimple] Rotated Yaw to: ", _yaw)
		elif event.keycode == KEY_EQUAL: # Zoom In (+)
			_zoom = max(10.0, _zoom - zoom_step)
			_camera.position.z = _zoom
			print("[MainSimple] Zoom In (Key): ", _zoom)
		elif event.keycode == KEY_MINUS: # Zoom Out (-)
			_zoom = min(1500.0, _zoom + zoom_step)
			_camera.position.z = _zoom
			print("[MainSimple] Zoom Out (Key): ", _zoom)

func _handle_input(delta):
	# Navigation
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		print("Input Left Click")
		var mouse_pos = get_viewport().get_mouse_position()
		var from = _camera.project_ray_origin(mouse_pos)
		var to = from + _camera.project_ray_normal(mouse_pos) * 2000
		
		# Fallback: Raycast to Plane(Vector3.UP, 0) since MU is flat-ish
		var plane = Plane(Vector3.UP, 0)
		var intersection = plane.intersects_ray(from, to - from)
		
		if intersection:
			if _state_machine:
				_state_machine.move_to(intersection)
		
		# Try physics raycast too just in case terrain has collider
		var space = get_world_3d().direct_space_state
		var query = PhysicsRayQueryParameters3D.create(from, to)
		var res = space.intersect_ray(query)
		if res and _state_machine:
			print("Raycast Hit: ", res.position)
			_state_machine.move_to(res.position)
	
	# Camera Settings Hotkeys (Debug)
	# Camera Settings Hotkeys (Debug - and Scroll)
	if Input.is_key_pressed(KEY_COMMA): # Decrease Zoom (Zoom IN)
		_zoom = max(10.0, _zoom - 200.0 * delta)
	if Input.is_key_pressed(KEY_PERIOD): # Increase Zoom (Zoom OUT)
		_zoom = min(1500.0, _zoom + 200.0 * delta)
	
	_camera.position.z = _zoom
	
	# Manual Camera Flip / Rotate (fine)
	if Input.is_key_pressed(KEY_Q): _yaw -= 90.0 * delta
	if Input.is_key_pressed(KEY_E): _yaw += 90.0 * delta

	# Manual WASD Movement
	if _is_debug_cam:
		var move_speed = _zoom * delta * 0.5 # Scale speed by zoom
		var move_dir = Vector3.ZERO
		
		# Movement is relative to current view
		# If Yaw is 0 (looking -Z), W is -Z.
		# If Yaw is 180 (looking +Z), W is +Z.
		var forward = Vector3(0, 0, -1).rotated(Vector3.UP, deg_to_rad(_yaw))
		var right = Vector3(1, 0, 0).rotated(Vector3.UP, deg_to_rad(_yaw))
		
		if Input.is_key_pressed(KEY_W): move_dir += forward
		if Input.is_key_pressed(KEY_S): move_dir -= forward
		if Input.is_key_pressed(KEY_A): move_dir -= right
		if Input.is_key_pressed(KEY_D): move_dir += right
		
		if move_dir.length() > 0:
			_debug_cam_pos += move_dir.normalized() * move_speed
		
		_camera_rig.position = _debug_cam_pos
	
	_camera_rig.rotation_degrees.y = _yaw
	_camera_rig.get_node("Pitch").rotation_degrees.x = _pitch
	
	if Input.is_key_pressed(KEY_F12):
		_take_screenshot()

func _take_screenshot(custom_name: String = ""):
	print("[MainSimple] Attempting to capture screenshot...")
	if not DirAccess.dir_exists_absolute(SCREENSHOT_DIR):
		var err = DirAccess.make_dir_recursive_absolute(SCREENSHOT_DIR)
		print("[MainSimple] Created screenshot dir. Error code: ", err)
		
	var viewport = get_viewport()
	if not viewport:
		print("[MainSimple] ERROR: Viewport not found!")
		return
		
	var texture = viewport.get_texture()
	if not texture:
		print("[MainSimple] ERROR: Texture not found!")
		return
		
	var image = texture.get_image()
	if not image:
		print("[MainSimple] ERROR: Image is null!")
		return
		
	var name_part = custom_name
	if name_part.is_empty():
		name_part = "screenshot_" + Time.get_datetime_string_from_system().replace(":", "-")
		
	var file_path = SCREENSHOT_DIR.path_join(name_part + ".png")
	var err = image.save_png(file_path)
	if err == OK:
		print("[MainSimple] Screenshot SUCCESSFULLY saved to: ", ProjectSettings.globalize_path(file_path))
		# Print name for agent detection
		print("IMAGE_SAVED: ", ProjectSettings.globalize_path(file_path))
	else:
		print("[MainSimple] FAILED to save screenshot. Error code: ", err)


func _notification(what):
	if what == NOTIFICATION_WM_CLOSE_REQUEST or what == NOTIFICATION_WM_GO_BACK_REQUEST:
		if _character:
			_save_state()

func _save_state():
	if not _character: return
	
	var data = {
		"player_x": _character.position.x,
		"player_y": _character.position.y,
		"player_z": _character.position.z,
		"cam_x": _debug_cam_pos.x,
		"cam_z": _debug_cam_pos.z,
		"cam_zoom": _zoom,
		"cam_yaw": _yaw
	}
	
	var file = FileAccess.open(SAVE_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data))
		_last_saved_pos = _character.position
		# print("[MainSimple] Saved state (including camera)")

func _load_state() -> Dictionary:
	if not FileAccess.file_exists(SAVE_PATH):
		return {}
		
	var file = FileAccess.open(SAVE_PATH, FileAccess.READ)
	if not file: return {}
	
	var content = file.get_as_text()
	var json = JSON.new()
	if json.parse(content) == OK:
		var data = json.get_data()
		if data is Dictionary:
			return data
	
	return {}

