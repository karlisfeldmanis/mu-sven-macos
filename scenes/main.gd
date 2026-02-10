extends Node3D

## Main Scene
## Entry point for the MU Online Remaster.
## Orhcestrates the world loading via MUAPI and MUHeightmap.

const MUAPI_CLASS = preload("res://addons/mu_tools/core/mu_api.gd")
const MUHeightmapClass = preload("res://addons/mu_tools/nodes/mu_heightmap.gd")

@export var world_id: int = 0:
	set(val):
		world_id = val
		if heightmap_node:
			heightmap_node.world_id = val

@export var data_path: String = "res://reference/MuMain/src/bin/Data"

# Camera Settings
@export var smoothing_speed: float = 5.0
@export var zoom_smoothing_speed: float = 8.0
@export var config_save_path: String = "user://camera_config.json"

var api: MUAPI_CLASS
var heightmap_node: MUHeightmapClass
var camera: Camera3D

var target_yaw: float = 0.0
var target_pitch: float = -45.0
var target_zoom: float = 180.0
var yaw: float = 0.0
var pitch: float = -45.0
var zoom_distance: float = 180.0
var focus_point: Vector3 = Vector3(128, 0, 128)

var is_right_click_down: bool = false
var _config_dirty: bool = false
var _save_timer: float = 0.0
var _inspect_label: Label3D

func _ready():
	get_window().size = Vector2i(1280, 720)
	get_window().mode = Window.MODE_WINDOWED
	print("[MU] Launching Modern Main Scene...")

	# 1. Initialize API
	api = MUAPI_CLASS.new()
	
	# 2. Setup Heightmap Node
	heightmap_node = MUHeightmapClass.new()
	heightmap_node.world_id = world_id
	heightmap_node.data_path = data_path
	add_child(heightmap_node)
	
	# 3. Setup Perspective Camera
	camera = Camera3D.new()
	add_child(camera)
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	camera.fov = 60.0
	camera.far = 2000.0
	camera.make_current()
	
	_load_camera_config()
	
	# Disable Debug Mode
	heightmap_node.set_debug_mode(0)
	
	_inspect_label = Label3D.new()
	_inspect_label.name = "InspectLabel"
	_inspect_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_inspect_label.no_depth_test = true
	_inspect_label.font_size = 32
	_inspect_label.outline_size = 8
	_inspect_label.pixel_size = 0.01
	_inspect_label.visible = false
	add_child(_inspect_label)

	print("✓ Main Scene Ready (Unified API).")

	# Auto-capture screenshot after scene loads (Skip in headless)
	if DisplayServer.get_name() != "headless":
		await get_tree().create_timer(3.0).timeout
		take_screenshot_to("main_screenshot.png")
		print("[Screenshot] Auto-screenshot saved")

func _process(delta):
	# 1. Smooth Camera Transition
	if camera:
		yaw = lerp_angle(deg_to_rad(yaw), deg_to_rad(target_yaw), smoothing_speed * delta)
		yaw = rad_to_deg(yaw)
		pitch = lerp(pitch, target_pitch, smoothing_speed * delta)
		zoom_distance = lerp(zoom_distance, target_zoom, zoom_smoothing_speed * delta)
		
		camera.rotation_degrees = Vector3(pitch, yaw, 0)
		var forward = -Basis.from_euler(camera.rotation).z
		camera.position = focus_point - forward * zoom_distance

	# 2. Movement Controls (WASD)
	var move_vec = Vector2.ZERO
	if Input.is_key_pressed(KEY_W): move_vec.y -= 1
	if Input.is_key_pressed(KEY_S): move_vec.y += 1
	if Input.is_key_pressed(KEY_A): move_vec.x -= 1
	if Input.is_key_pressed(KEY_D): move_vec.x += 1
	
	if move_vec != Vector2.ZERO:
		var cam_basis = camera.global_transform.basis
		var forward = Vector3(cam_basis.z.x, 0, cam_basis.z.z).normalized()
		var right = Vector3(cam_basis.x.x, 0, cam_basis.x.z).normalized()
		var move_dir = (right * move_vec.x + forward * move_vec.y).normalized()
		
		var speed = 15.0 * delta
		if Input.is_key_pressed(KEY_SHIFT): speed *= 3.0
		focus_point += move_dir * speed
		_config_dirty = true
	
	# 3. Persistence
	if _config_dirty:
		_save_timer += delta
		if _save_timer > 2.0:
			_save_camera_config()
			_save_timer = 0.0

func _input(event):
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			is_right_click_down = event.pressed
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if is_right_click_down \
					else Input.MOUSE_MODE_VISIBLE
				
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			target_zoom = clamp(target_zoom - 10.0, 2.0, 1000.0)
			_config_dirty = true
		if event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			target_zoom = clamp(target_zoom + 10.0, 2.0, 1000.0)
			_config_dirty = true

	if event is InputEventMouseMotion and is_right_click_down:
		var sensitivity = 0.15
		target_yaw -= event.relative.x * sensitivity
		target_pitch = clamp(target_pitch - event.relative.y * sensitivity, -89.0, -10.0)
		_config_dirty = true
		
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
		_inspect_object(event.position)
		
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_P:
			if DisplayServer.get_name() != "headless":
				take_screenshot_to()

func _inspect_object(screen_pos: Vector2):
	var from = camera.project_ray_origin(screen_pos)
	var to = from + camera.project_ray_normal(screen_pos) * 5000.0
	
	var space_state = get_world_3d().direct_space_state
	var query = PhysicsRayQueryParameters3D.create(from, to)
	var result = space_state.intersect_ray(query)
	
	if result:
		var collider = result.collider
		var p_world = result.position
		
		# Convert to MU Grid
		var mu_x = int(clamp(floor(p_world.x), 0, 255))
		var mu_y = int(clamp(floor(p_world.z), 0, 255))
		
		_inspect_label.text = "Object: %s\nMU Tile: (%d, %d)\nWorld: %s" % [
			collider.name, mu_x, mu_y, 
			str(p_world).replace("(", "").replace(")", "")
		]
		_inspect_label.global_position = p_world + Vector3(0, 5, 0)
		_inspect_label.visible = true
	else:
		_inspect_label.visible = false

func take_screenshot_to(target_path: String = ""):
	var img = get_viewport().get_texture().get_image()
	if img:
		if target_path.is_empty():
			if not DirAccess.dir_exists_absolute("res://screenshots"):
				DirAccess.make_dir_absolute("res://screenshots")
			target_path = "res://screenshots/mu_%s.png" % \
					Time.get_datetime_string_from_system().replace(":", "-")
		img.save_png(target_path)
		print("✓ Screenshot: ", target_path)

func _save_camera_config():
	var file = FileAccess.open(config_save_path, FileAccess.WRITE)
	if file:
		var data = {
			"yaw": target_yaw, "pitch": target_pitch, "zoom": target_zoom,
			"fx": focus_point.x, "fz": focus_point.z
		}
		file.store_string(JSON.stringify(data))
		_config_dirty = false

func _load_camera_config():
	if FileAccess.file_exists(config_save_path):
		var file = FileAccess.open(config_save_path, FileAccess.READ)
		var json = JSON.new()
		if json.parse(file.get_as_text()) == OK:
			var data = json.data
			target_yaw = data.get("yaw", target_yaw)
			target_pitch = data.get("pitch", target_pitch)
			target_zoom = data.get("zoom", target_zoom)
			focus_point.x = data.get("fx", focus_point.x)
			focus_point.z = data.get("fz", focus_point.z)
			yaw = target_yaw
			pitch = target_pitch
			zoom_distance = target_zoom
