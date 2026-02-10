extends Node3D

## Main Game Scene (Standalone App)
##
## This is the entry point for the standalone Godot application
## Runs without the editor, loads assets at runtime

const MUHeightmap = preload("res://addons/mu_tools/nodes/mu_heightmap.gd")

var _terrain: MUHeightmap
var _camera_rig: Node3D
var _camera: Camera3D

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
	
	# Load saved camera view
	_load_camera_view()

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

