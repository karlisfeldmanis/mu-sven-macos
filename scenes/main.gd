extends Node3D

## Visual Rendering Test
## Loads BMD with skeleton + meshes for visual inspection

@export_file("*.bmd") var bmd_file_path: String = "res://raw_data/Player/ArmorClass01.bmd"

var _anim_player: AnimationPlayer
var _current_action: int = 0
var _state_machine: MUStateMachine

const MUCoordinateUtils = preload("res://addons/mu_tools/coordinate_utils.gd")
const MUAnimationRegistry = preload("res://addons/mu_tools/mu_animation_registry.gd")
const MUStateMachine = preload("res://addons/mu_tools/mu_state_machine.gd")
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")
const MURenderSettings = preload("res://addons/mu_tools/mu_render_settings.gd")

var _character: Node3D
var _terrain: Node # MUTerrain instance

# Camera Settings (Authentic MU Settings)
var _camera_yaw: Node3D
var _camera_pitch: Node3D
var _camera: Camera3D

var _pitch_val: float = -45.0  # Natural 45-degree angle
var _yaw_val: float = 0.0  # No rotation
var _distance_val: float = 200.0  # High above terrain
var _fov_val: float = 30.0  # Authentic narrow perspective
var _zoom_val: float = 256.0 # Orthographic size (higher = zoomed out)

var _camera_speed: float = 40.0 # Spectator speed
var _is_free_cam: bool = true # Default to free cam
var _mouse_sensitivity: float = 0.15
var _camera_velocity: Vector3 = Vector3.ZERO
var _friction: float = 6.0
var _acceleration: float = 120.0
var _is_rmb_down: bool = false

var _fsr_mode = MURenderSettings.QualityMode.NATIVE

func _ready() -> void:
	MULogger.init()
	MULogger.info("Application starting...")
	
	# Handle Heartbeat for launch verification
	_check_heartbeat()
	
	# 0. Strict Resource Pre-flight
	if not _validate_critical_resources():
		var msg = "FATAL: Critical script or shader errors detected. Aborting launch."
		MULogger.error(msg)
		push_error("[Render Test] " + msg)
		return
		
	_setup_camera()
	_setup_sun()
	_setup_environment()
	_setup_falling_leaves()
	_add_lorencia_terrain()
	
	# Initialize FSR
	MURenderSettings.set_quality_mode(get_viewport(), _fsr_mode)
	
	_update_camera_rig()
	
	MULogger.info("Scene tree initialization complete. Waiting for first frames...")
	print("[Controls] VIEW: Press 'V' to toggle Perspective/Orthographic")
	print("[Controls] Press 'P' to take a manual screenshot")

func _check_heartbeat():
	# Wait for two frames to ensure rendering has actually started
	await get_tree().process_frame
	await get_tree().process_frame
	MULogger.info("HEARTBEAT_SUCCESS: First frames rendered successfully.")


func _setup_sun() -> void:
	# Clean up any existing lights
	for child in get_children():
		if child is DirectionalLight3D:
			child.queue_free()
	
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.rotation_degrees = Vector3(-60, 45, 0)
	sun.light_energy = 1.0 # Standard energy
	sun.light_indirect_energy = 0.5
	sun.shadow_enabled = true
	
	# Performance optimized settings (High Quality Defaults)
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.directional_shadow_max_distance = 120.0
	sun.shadow_blur = 0.5
	sun.shadow_bias = 0.1
	
	add_child(sun)
	print("[Main] Created optimized Sun")

func _setup_environment() -> void:
	# Clean up existing environment
	for child in get_children():
		if child is WorldEnvironment:
			child.queue_free()
			
	var env = Environment.new()
	
	# Fog Settings (Authentic Lorencia Dark Brown)
	env.fog_enabled = true
	# SVEN: (30, 20, 10) / 256
	env.fog_light_color = Color(30.0/255.0, 20.0/255.0, 10.0/255.0)
	env.fog_density = 0.002 # Subtle depth
	env.fog_aerial_perspective = 0.5
	env.fog_sky_affect = 0.2
	
	# Global Illumination & Background
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(30.0/255.0, 20.0/255.0, 10.0/255.0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.2, 0.2, 0.2)
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	
	var world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)
	print("[Main] Environment configured (Lorencia Fog)")

func _setup_falling_leaves() -> void:
	var leaves_script = load("res://scenes/lorencia_effects/falling_leaves.gd")
	if leaves_script:
		var leaves = GPUParticles3D.new()
		leaves.set_script(leaves_script)
		add_child(leaves)
		print("[Main] Falling leaves effect added")
	else:
		push_error("[Main] Failed to load falling_leaves.gd")

func _validate_critical_resources() -> bool:
	var resources = [
		"res://addons/mu_tools/mu_terrain.gd",
		"res://addons/mu_tools/mu_terrain_parser.gd",
		"res://addons/mu_tools/coordinate_utils.gd",
		"res://addons/mu_tools/mu_texture_loader.gd",
		"res://addons/mu_tools/mesh_builder.gd",
		"res://addons/mu_tools/mu_preflight.gd",
		"res://core/shaders/mu_terrain.gdshader",
		"res://core/shaders/mu_character.gdshader",
		"res://scenes/lorencia_effects/mu_fire.gd",
		"res://scenes/lorencia_effects/shaders/mu_fire.gdshader"
	]
	
	print("[Pre-flight] Validating critical resources...")
	var all_ok = true
	for res_path in resources:
		if not FileAccess.file_exists(res_path):
			push_error("  [Pre-flight] ERROR: File not found: " + res_path)
			all_ok = false
			continue
			
		var res = load(res_path)
		if not res:
			push_error("  [Pre-flight] ERROR: Failed to load/parse: " + res_path)
			all_ok = false
		else:
			print("  [Pre-flight] OK: " + res_path)
			
	return all_ok

func _add_lorencia_terrain() -> void:
	var mut_script = load("res://addons/mu_tools/mu_terrain.gd")
	_terrain = mut_script.new()
	_terrain.world_id = 1
	# Set data_path to the base 'Data' directory instead of 'World1'
	_terrain.data_path = "res://reference/MuMain/src/bin/Data"
	add_child(_terrain)
	_terrain.load_world()

func _get_terrain_height(tile_x: int, tile_y: int) -> float:
	if not _terrain or not _terrain.heightmap:
		return 0.0
	
	var idx = MUCoordinateUtils.tile_to_index(tile_x, tile_y)
	if idx >= 0 and idx < _terrain.heightmap.size():
		return _terrain.heightmap[idx]
	return 0.0

func _take_screenshot(filename: String = "screenshot.png") -> void:
	await get_tree().process_frame # Ensure rendering is finished
	await get_tree().process_frame
	var viewport = get_viewport()
	var texture = viewport.get_texture()
	if not texture:
		print("[Render Test] ERROR: Viewport texture is null")
		return
	var img = texture.get_image()
	if not img:
		print("[Render Test] ERROR: Viewport image is null")
		return
	img.save_png(filename)
	print("[Render Test] Screenshot saved to: ", filename)

func _setup_camera() -> void:
	# Clean up any existing cameras in the scene
	for child in get_children():
		if child is Camera3D: child.queue_free()
		if child.name.begins_with("Camera"): child.queue_free()

	_camera_yaw = Node3D.new()
	_camera_yaw.name = "CameraYaw"
	add_child(_camera_yaw)
	
	_camera_pitch = Node3D.new()
	_camera_pitch.name = "CameraPitch"
	_camera_yaw.add_child(_camera_pitch)
	
	_camera = Camera3D.new()
	_camera.name = "MainCamera"
	_camera.current = true # CRITICAL: Ensure this camera is used
	_camera_pitch.add_child(_camera)
	
	# Load saved camera state
	_load_camera_state()
	
	_update_camera_rig()

func _update_camera_rig() -> void:
	_camera_yaw.rotation_degrees.y = _yaw_val
	_camera_pitch.rotation_degrees.x = _pitch_val
	
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.fov = 75.0
	_camera.position = Vector3.ZERO # True free cam: No orbital offset

func _process(delta: float) -> void:
	_handle_spectator_movement(delta)
	
	if not _is_free_cam and _character:
		# Smoothly follow character
		_camera_yaw.position = _camera_yaw.position.lerp(_character.position, delta * 10.0)

func _exit_tree() -> void:
	# Save camera state on exit
	_save_camera_state()

func _save_camera_state() -> void:
	var config = ConfigFile.new()
	config.set_value("camera", "position", _camera_yaw.position)
	config.set_value("camera", "yaw", _yaw_val)
	config.set_value("camera", "pitch", _pitch_val)
	config.set_value("camera", "zoom", _zoom_val)
	config.save("user://camera_state.cfg")
	print("[Render Test] Saved camera: pos=", _camera_yaw.position, 
				" yaw=", _yaw_val, " pitch=", _pitch_val, " zoom=", _zoom_val)

func _load_camera_state() -> void:
	var config = ConfigFile.new()
	var err = config.load("user://camera_state.cfg")
	if err == OK:
		_camera_yaw.position = config.get_value("camera", "position", Vector3(128, 50, -128))
		_yaw_val = config.get_value("camera", "yaw", 0.0)
		_pitch_val = config.get_value("camera", "pitch", -45.0)
		_zoom_val = config.get_value("camera", "zoom", 50.0)
		print("[Render Test] Loaded camera: pos=", _camera_yaw.position, 
				" yaw=", _yaw_val, " pitch=", _pitch_val, " zoom=", _zoom_val)
	else:
		# First run - set defaults
		_camera_yaw.position = Vector3(128, 50, -128)
		_zoom_val = 50.0
		print("[Render Test] No saved camera state, using defaults")

func _handle_spectator_movement(delta: float) -> void:
	# Even if not in free cam, allow movement to break out into free cam
	# This prevents the "stuck" feeling the user described.
	var wish_dir = Vector3.ZERO
	if Input.is_key_pressed(KEY_W): wish_dir += -_camera.global_transform.basis.z
	if Input.is_key_pressed(KEY_S): wish_dir += _camera.global_transform.basis.z
	if Input.is_key_pressed(KEY_A): wish_dir += -_camera.global_transform.basis.x
	if Input.is_key_pressed(KEY_D): wish_dir += _camera.global_transform.basis.x
	if Input.is_key_pressed(KEY_E): wish_dir += Vector3.UP
	if Input.is_key_pressed(KEY_Q): wish_dir += Vector3.DOWN
	
	if wish_dir != Vector3.ZERO:
		if not _is_free_cam:
			_is_free_cam = true
			print("[Camera] WASD detected. Entering Free Cam mode.")
		
		wish_dir = wish_dir.normalized()
		
		var accel = _acceleration
		if Input.is_key_pressed(KEY_SHIFT):
			accel *= 4.0
		
		_camera_velocity += wish_dir * accel * delta
	
	# Apply friction
	_camera_velocity = _camera_velocity.lerp(Vector3.ZERO, _friction * delta)
	
	# Apply velocity
	_camera_yaw.global_position += _camera_velocity * delta

func _handle_camera_movement(_delta: float) -> void:
	pass # Deprecated in favor of spectator logic

func _on_speed_changed(val: float) -> void:
	if _anim_player:
		_anim_player.speed_scale = val



func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		if event.keycode >= KEY_0 and event.keycode <= KEY_9:
			var action_idx = event.keycode - KEY_0
			play_action(action_idx)
		
		# Simulation shortcuts
		if event.keycode == KEY_W and event.shift_pressed: # Avoid conflict with WASD
			_cycle_weapon()
		if event.keycode == KEY_S and event.shift_pressed: # Avoid conflict with WASD
			_toggle_safe_zone()
		if event.keycode == KEY_C:
			_is_free_cam = !_is_free_cam
			print("[Camera] Mode: ", "Free" if _is_free_cam else "Follow")
			print("[Camera] Mode: ", "Free" if _is_free_cam else "Follow")
		if event.keycode == KEY_P:
			_take_screenshot("manual_screenshot_%d.png" % int(Time.get_unix_time_from_system()))
		
		# FSR Control
		if event.keycode == KEY_F2:
			_fsr_mode = MURenderSettings.cycle_mode(get_viewport(), _fsr_mode)
			
		if event.keycode == KEY_V:
			if _camera.projection == Camera3D.PROJECTION_PERSPECTIVE:
				_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
				_camera.size = _zoom_val
				print("[Camera] Switched to Orthographic (Size: %.1f)" % _zoom_val)
			else:
				_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
				_camera.fov = 75.0
				print("[Camera] Switched to Perspective (FOV: 75)")
				
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			_is_rmb_down = event.pressed
			if _is_rmb_down:
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			else:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
				
		if event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			var screen_size = get_viewport().get_visible_rect().size
			var pos = event.position if not _is_rmb_down else screen_size / 2.0
			_perform_object_picking(pos)

	if event is InputEventMouseMotion and _is_rmb_down:
		_yaw_val -= event.relative.x * _mouse_sensitivity
		_pitch_val -= event.relative.y * _mouse_sensitivity
		_pitch_val = clamp(_pitch_val, -89.0, 89.0)
		
		_camera_yaw.rotation_degrees.y = _yaw_val
		_camera_pitch.rotation_degrees.x = _pitch_val

func _perform_object_picking(mouse_pos: Vector2) -> void:
	var from = _camera.project_ray_origin(mouse_pos)
	var to = from + _camera.project_ray_normal(mouse_pos) * 1000.0
	
	var space_state = get_world_3d().direct_space_state
	var query = PhysicsRayQueryParameters3D.create(from, to)
	var result = space_state.intersect_ray(query)
	
	if result:
		var collider = result.collider
		var bmd_path = collider.get_meta("bmd_path", "Unknown")
		var mu_euler = collider.get_meta("mu_euler", Vector3.ZERO)
		var mu_pos = collider.get_meta("mu_pos", Vector3.ZERO)
		var obj_name = collider.get_parent().name
		
		print("\n[Object Picked]")
		print("  Name: ", obj_name)
		print("  BMD Path: ", bmd_path)
		print("  MU Angle: ", mu_euler)
		print("  MU Pos: ", mu_pos)
		print("  Godot Pos: ", collider.global_position)
		
		# Visual feedback: Flash the object or show label
		# For now, just print is enough as requested
	else:
		print("[Picking] No object hit")

func _cycle_weapon() -> void:
	var weapons = ["None", "Sword", "TwoHandSword", "Spear", "Bow", "Staff"]
	var idx = weapons.find(_state_machine.weapon_class)
	_state_machine.weapon_class = weapons[(idx + 1) % weapons.size()]
	_state_machine.update_animation()

func _toggle_safe_zone() -> void:
	_state_machine.is_safe_zone = !_state_machine.is_safe_zone
	_state_machine.update_animation()

func play_action(idx: int) -> void:
	if not _anim_player: return
	var anim_name = MUAnimationRegistry.get_action_name(bmd_file_path, idx)
	if _anim_player.has_animation(anim_name):
		_anim_player.play(anim_name)
		_current_action = idx
		
		# Reset to 1.0x (now authentic because speeds are baked)
		_on_speed_changed(1.0)
		
		print("▶ Switching to: ", anim_name, " (Speed: 1.0x - Authentic)")
	else:
		# Fallback to index if mapping fails or list is short
		var fallback = "Action_%d" % idx
		if _anim_player.has_animation(fallback):
			_anim_player.play(fallback)
			print("▶ Switching to (fallback): ", fallback)

func load_character_with_state_machine(path: String) -> void:
	print("Loading Character with State Machine...")
	
	# For player models, the skeleton and animations are in player.bmd
	var base_path = path
	if "player/" in path.to_lower():
		base_path = "res://raw_data/Player/player.bmd"
	
	var skeleton = load_base_skeleton(base_path)
	if not skeleton:
		return
		
	# If the original path was a part (like Armor), attach it too
	if path != base_path:
		attach_bmd_meshes(path, skeleton)
		
	# Load standard armor parts for visualization
	var parts = [
		"res://raw_data/Player/ArmorClass01.bmd",
		"res://raw_data/Player/PantClass01.bmd",
		"res://raw_data/Player/GloveClass01.bmd",
		"res://raw_data/Player/BootClass01.bmd",
		"res://raw_data/Player/HelmClass01.bmd"
	]
	
	for part_path in parts:
		if part_path != path and FileAccess.file_exists(part_path):
			attach_bmd_meshes(part_path, skeleton)
	
	# Add skeleton visualization (Hidden for clean view)
	# _add_skeleton_visualization(skeleton)
	# Apply MU Native Class Scale (Default 1.2x for DK/general)
	# Reference: ZzzCharacter.cpp -> SetCharacterScale
	_character.scale = Vector3(1.2, 1.2, 1.2)
	
	# Setup State Machine
	_state_machine = MUStateMachine.new()
	add_child(_state_machine)
	_state_machine.setup(_anim_player, base_path)
	_state_machine.weapon_class = "None" # Start clean
	_state_machine.set_state(MUStateMachine.State.IDLE)
	_state_machine.update_animation()
	
	# Measure total height for scale verification
	var aabb = _get_total_aabb(skeleton)
	print("\n[Scale Verification] Character height: %.2fm" % aabb.size.y)
	
	print("\n=== Character loaded and idling! ===")

func _get_total_aabb(node: Node3D) -> AABB:
	var total_aabb = AABB()
	var first = true
	
	# Recursively find all MeshInstances
	var mesh_nodes = []
	var skeleton: Skeleton3D = null
	var stack = [node]
	while not stack.is_empty():
		var n = stack.pop_back()
		if n is MeshInstance3D and n.mesh:
			mesh_nodes.append(n)
		if n is Skeleton3D:
			skeleton = n
		for child in n.get_children():
			stack.append(child)
	
	# Force update to ensure bone poses are calculated
	if skeleton:
		var bone_heights = []
		# Measure height using bone rest positions
		for i in range(skeleton.get_bone_count()):
			var bone_rest_global = skeleton.get_bone_global_rest(i)
			var bone_pos = skeleton.global_transform * bone_rest_global.origin
			bone_heights.append(bone_pos.y)
			if first:
				total_aabb = AABB(bone_pos, Vector3.ZERO)
				first = false
			else:
				total_aabb = total_aabb.expand(bone_pos)
	
	return total_aabb

func _add_ref_post(pos: Vector3, text: String, height: float, color: Color) -> void:
	var mesh_instance = MeshInstance3D.new()
	var cylinder = CylinderMesh.new()
	cylinder.top_radius = 0.05
	cylinder.bottom_radius = 0.05
	cylinder.height = height
	mesh_instance.mesh = cylinder
	mesh_instance.position = pos + Vector3(0, height/2.0, 0)
	var mat = StandardMaterial3D.new()
	mat.albedo_color = color
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mesh_instance.material_override = mat
	add_child(mesh_instance)
	
	var label = Label3D.new()
	label.text = text
	label.position = pos + Vector3(0, height + 0.2, 0)
	label.pixel_size = 0.005
	add_child(label)


func load_base_skeleton(path: String) -> Skeleton3D:
	var parser = BMDParser.new()
	if not parser.parse_file(path, false): # debug=false for clean view
		push_error("Failed to parse base BMD: " + path)
		return null
	
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	if not skeleton:
		push_error("Failed to build skeleton")
		return null
		
	# Create character root with Z-up to Y-up rotation
	_character = Node3D.new()
	_character.name = "Character"
	# Instead of rotating the character root which messes with local movement,
	# we wrap the skeleton in a container that handles the MU-to-Godot rotation.
	var mu_container = Node3D.new()
	mu_container.name = "MUContainer"
	# Vertex coordinates are now baked MU->Godot in MUCoordinateUtils
	mu_container.rotation.x = 0 
	_character.add_child(mu_container)
	add_child(_character)
	
	skeleton.name = "Skeleton"
	mu_container.add_child(skeleton)
	
	# 1. Setup AnimationPlayer
	_anim_player = AnimationPlayer.new()
	_anim_player.name = "AnimationPlayer"
	mu_container.add_child(_anim_player)
	
	# 2. Build Animations
	const AnimationBuilder = preload("res://addons/mu_tools/animation_builder.gd")
	var library = AnimationBuilder.build_animation_library(path, parser.actions, skeleton)
	_anim_player.add_animation_library("", library)
	
	# 3. Add meshes for the base model (e.g. skin, head)
	for bmd_mesh in parser.meshes:
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, path)
		if mesh_instance:
			skeleton.add_child(mesh_instance)
	
	# 4. Default Idle Loop (Action 1 = Stop_Male)
	var idle_name = MUAnimationRegistry.get_action_name(path, 1)
	if _anim_player.has_animation(idle_name):
		_anim_player.play(idle_name)
		_current_action = 1
	
	return skeleton

func attach_bmd_meshes(path: String, skeleton: Skeleton3D) -> void:
	var parser = BMDParser.new()
	if not parser.parse_file(path, false): # debug=false
		return
		
	for i in range(parser.meshes.size()):
		var bmd_mesh = parser.meshes[i]
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, path)
		if mesh_instance:
			mesh_instance.name = path.get_file().get_basename() + "_%d" % i
			skeleton.add_child(mesh_instance)
			
			# Explicitly set skeleton path AFTER adding to tree
			mesh_instance.skeleton = mesh_instance.get_path_to(skeleton)
			if mesh_instance.mesh and mesh_instance.get_surface_override_material(0):
				var mat = mesh_instance.get_surface_override_material(0)
				var tex = mat.get_shader_parameter("albedo_texture")
				print("  [Mesh %d] Attached with material: %s" % [i, tex.resource_path.get_file()])

func _add_skeleton_visualization(skeleton: Skeleton3D) -> void:
	const Visualizer = preload("res://addons/mu_tools/skeleton_visualizer.gd")
	var visualizer = Visualizer.new()
	visualizer.name = "SkeletonVisualizer"
	visualizer.skeleton = skeleton
	visualizer.bone_size = 0.02
	visualizer.line_thickness = 0.005
	skeleton.add_child(visualizer)
	
	print("✓ Added dynamic skeleton visualization")
