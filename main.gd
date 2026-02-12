extends Node3D

## Main Game Scene (Standalone App)
##
## This is the entry point for the standalone Godot application
## Runs without the editor, loads assets at runtime

const MUHeightmap = preload("res://addons/mu_tools/world/heightmap_node.gd")
const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/rendering/bmd_mesh_builder.gd")
const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")

var _terrain: MUHeightmap
var _objects_parent: Node3D
var _camera_rig: Node3D
var _camera: Camera3D
var _animated_materials: Array = []

const SAVE_PATH = "user://camera_view.cfg"

# Camera Settings
var _yaw: float = 0.0 
var _pitch: float = -45.0 
var _zoom: float = 100.0 
var _target_zoom: float = 100.0

func _ready() -> void:
	print("[MU Remaster] Starting standalone application (Terrain Only)...")
	print("  Godot Version: ", Engine.get_version_info().string)
	
	_setup_environment()
	_setup_camera()
	_load_terrain()
	
	# Wait for terrain to be ready before spawning objects
	await get_tree().process_frame
	_spawn_city_objects()
	
	# Load saved camera view
	_load_camera_view()
	
	# Handle CLI arguments
	_handle_cli_args()

func _capture_screenshot(fname: String) -> void:
	var img = get_viewport().get_texture().get_image()
	var path = "user://" + fname
	img.save_png(path)
	print("[MU Remaster] Saved: ", OS.get_user_data_dir() + "/" + fname)

func _notification(what):
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		_save_camera_view()

func _save_camera_view() -> void:
	var config = ConfigFile.new()
	config.set_value("camera", "position", _camera_rig.position)
	config.set_value("camera", "yaw", _yaw)
	config.set_value("camera", "pitch", _pitch)
	config.set_value("camera", "zoom", _target_zoom)
	var err = config.save(SAVE_PATH)
	if err == OK:
		print("[MU Remaster] Camera view saved to: ", SAVE_PATH)

func _load_camera_view() -> void:
	var config = ConfigFile.new()
	var err = config.load(SAVE_PATH)
	if err == OK:
		_camera_rig.position = config.get_value("camera", "position", _camera_rig.position)
		_yaw = config.get_value("camera", "yaw", _yaw)
		_pitch = config.get_value("camera", "pitch", _pitch)
		_target_zoom = config.get_value("camera", "zoom", _target_zoom)
		_zoom = _target_zoom
		print("[MU Remaster] Camera view loaded from: ", SAVE_PATH)

func _setup_environment() -> void:
	# Clean up existing environment
	for child in get_children():
		if child is WorldEnvironment:
			child.queue_free()
		if child is DirectionalLight3D:
			child.queue_free()
			
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.1, 0.1, 0.12)
	
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.3, 0.3, 0.35)
	env.ambient_light_energy = 0.8
	
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.ssao_enabled = true
	
	var world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)
	
	# Add Directional Light
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.rotation_degrees = Vector3(-60, 45, 0)
	sun.light_energy = 1.0
	sun.shadow_enabled = true
	add_child(sun)

func _setup_camera() -> void:
	# Clear existing camera if any
	for child in get_children():
		if child is Camera3D:
			child.queue_free()

	_camera_rig = Node3D.new()
	_camera_rig.name = "CameraRig"
	add_child(_camera_rig)
	
	var pitch_node = Node3D.new()
	pitch_node.name = "Pitch"
	_camera_rig.add_child(pitch_node)
	
	_camera = Camera3D.new()
	_camera.name = "MainCamera"
	_camera.current = true 
	_camera.position.z = _zoom
	_camera.far = 2000.0 
	
	pitch_node.add_child(_camera)
	
	# Initial Position (Center of 256x256 terrain)
	_camera_rig.position = Vector3(150, 0, 150)
	_camera_rig.rotation_degrees.y = _yaw
	pitch_node.rotation_degrees.x = _pitch

func _load_terrain() -> void:
	_terrain = MUHeightmap.new()
	_terrain.name = "Terrain"
	_terrain.world_id = 0
	_terrain.data_path = "res://reference/MuMain/src/bin/Data" 
	add_child(_terrain)

func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_target_zoom = max(10.0, _target_zoom - 10.0)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_target_zoom = min(300.0, _target_zoom + 10.0)
	
	if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		_yaw -= event.relative.x * 0.25
		_pitch -= event.relative.y * 0.25
		_pitch = clamp(_pitch, -89.0, -5.0)
			
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_ESCAPE:
			_save_camera_view()
			get_tree().quit()
		if event.keycode == KEY_R: # Refresh objects
			_spawn_city_objects()

func _process(delta: float) -> void:
	# Simple smooth zoom
	_zoom = lerp(_zoom, _target_zoom, 5.0 * delta)
	if _camera:
		_camera.position.z = _zoom
		
	# Update Rotation
	if _camera_rig:
		var pitch_node = _camera_rig.get_node("Pitch")
		# Smoothly interpolate or direct set? Direct set for responsiveness is fine for now, 
		# but let's use standard lerp for smoothness if desired. 
		# For now, distinct controls: direct set from mouse, smooth zoom.
		# To avoid jitter, we can just set it.
		_camera_rig.rotation_degrees.y = _yaw
		pitch_node.rotation_degrees.x = _pitch
		
	# Simple WASD movement
	var speed = 50.0 * delta
	var move_vec = Vector3.ZERO
	if Input.is_key_pressed(KEY_W): move_vec.z -= 1
	if Input.is_key_pressed(KEY_S): move_vec.z += 1
	if Input.is_key_pressed(KEY_A): move_vec.x -= 1
	if Input.is_key_pressed(KEY_D): move_vec.x += 1
	
	if move_vec.length() > 0:
		move_vec = move_vec.rotated(Vector3.UP, deg_to_rad(_yaw))
		_camera_rig.position += move_vec * speed

func _handle_cli_args() -> void:
	var args = OS.get_cmdline_args()
	var out_path = ""
	
	for i in range(args.size()):
		if args[i] == "--out" and i + 1 < args.size():
			out_path = args[i+1]
		if args[i] == "--camera" and i + 1 < args.size():
			var c_args = args[i+1].split(",")
			if c_args.size() >= 3:
				_camera_rig.position = Vector3(float(c_args[0]), float(c_args[1]), float(c_args[2]))
			if c_args.size() >= 6:
				_yaw = float(c_args[3])
				_pitch = float(c_args[4])
				_target_zoom = float(c_args[5])
				_zoom = _target_zoom
				
	if out_path != "":
		# Wait for render
		await get_tree().process_frame
		await get_tree().process_frame
		
		var img = get_viewport().get_texture().get_image()
		img.save_png(out_path)
		print("[MU Remaster] CLI Screenshot saved to: ", out_path)
		get_tree().quit()

func _spawn_city_objects() -> void:
	if not _terrain: return
	
	print("[MU Remaster] Spawning objects in city center...")
	
	# 1. Setup Container
	if _objects_parent:
		_objects_parent.queue_free()
	_objects_parent = Node3D.new()
	_objects_parent.name = "Objects"
	add_child(_objects_parent)
	
	# 2. Get Data
	var objects = _terrain.get_objects_data()
	# var city_center = Vector3(128, 0, 128) # MU Transposed
	# var radius = 64.0 # Range around center
	
	var spawn_count = 0
	var model_cache = {} # path -> {parser, skeleton?}
	
	for obj in objects:
		# var dist = obj.position.distance_to(city_center)
		# if dist > radius: continue
		
		# 3. Resolve Path
		var bmd_path = MUModelRegistry.get_object_path(obj.type, _terrain.world_id)
		if bmd_path == "": continue
		
		# 4. Load/Cache Model
		if not model_cache.has(bmd_path):
			var parser = BMDParser.new()
			if parser.parse_file(bmd_path, false):
				model_cache[bmd_path] = parser
			else:
				model_cache[bmd_path] = null
				
		var parser = model_cache[bmd_path]
		if not parser: continue
		
		# 5. Instantiate Meshes (Bake Pose for performance in static scene)
		var obj_node = Node3D.new()
		obj_node.name = "Obj_%d_T%d" % [spawn_count, obj.type]
		obj_node.position = obj.position
		obj_node.quaternion = obj.rotation
		obj_node.scale = Vector3(obj.scale, obj.scale, obj.scale)
		_objects_parent.add_child(obj_node)
		
		# Apply registry overrides
		var rot_override = MUModelRegistry.get_rotation_override(bmd_path)
		if rot_override != Vector3.ZERO:
			obj_node.rotation_degrees += rot_override
		
		var bind_pose = MUModelRegistry.get_bind_pose_action(bmd_path, 0)
		
		for i in range(parser.meshes.size()):
			var mesh_instance = MUMeshBuilder.create_mesh_instance_v2(
				parser.meshes[i], null, bmd_path, parser, true, true, false, i, bind_pose, _animated_materials
			)
			if mesh_instance:
				var mat = mesh_instance.get_active_material(0)
				# 🔴 FIX: PARITY MESH HIDING (Phase 2)
				if mat and mat.get_meta("mu_script_hidden", false):
					mesh_instance.free()
					continue
					
				obj_node.add_child(mesh_instance)
				
				# Apply BlendMesh logic
				var blend_idx = MUModelRegistry.get_blend_mesh_index(bmd_path)
				if i == blend_idx and mat is StandardMaterial3D:
					mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
					mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		
		spawn_count += 1
		
	print("  ✓ Spawned %d objects in city radius." % spawn_count)

