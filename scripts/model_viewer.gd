extends Node3D

## Model Viewer Scene Script
## Simulates the main scene environment (lighting, sky, fog)
## without terrain, for previewing how OBJ assets will render
## in the actual game world.

const MUObjLoader = preload(
	"res://addons/mu_tools/core/mu_obj_loader.gd"
)
const MUEnvironmentClass = preload(
	"res://addons/mu_tools/nodes/mu_environment.gd"
)

@onready var model_root = $ModelRoot
@onready var camera = $OrbitCamera/Camera3D
@onready var orbit_node = $OrbitCamera

# Orbital camera state
var _yaw: float = 45.0
var _pitch: float = -30.0
var _distance: float = 5.0
var _target_yaw: float = 45.0
var _target_pitch: float = -30.0
var _target_distance: float = 5.0
var _orbit_center: Vector3 = Vector3.ZERO
var _min_distance: float = 0.5
var _max_distance: float = 50.0

# UI
var _selector: OptionButton
var _model_paths: Array[String] = []
var _sim_hud: Label
var _gizmo: Node3D


func _ready():
	_setup_environment()
	_setup_ui()
	_setup_gizmo()


func _setup_environment():
	# --- Match main.tscn camera ---
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	camera.fov = 60.0
	camera.far = 2000.0

	# Setup environment (Lorencia style)
	var env = MUEnvironmentClass.new()
	env.name = "Environment"
	add_child(env)
	env.setup_environment(1) 
	# Disable fog so raw-scale models (which can be huge/distant) aren't hidden
	env.disable_fog()

	# --- DirectionalLight3D (exact copy from main.tscn) ---
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.transform = Transform3D(
		Vector3(0.866025, -0.433013, 0.25),
		Vector3(0, 0.5, 0.866025),
		Vector3(-0.5, -0.75, 0.433013),
		Vector3(0, 3, 2)
	)
	sun.shadow_opacity = 0.5
	add_child(sun)


func _setup_gizmo():
	_gizmo = Node3D.new()
	_gizmo.name = "XYZGizmo"
	add_child(_gizmo)
	
	# X Axis (Red) - MU-X (Godot +X)
	_create_gizmo_line(Vector3.ZERO, Vector3(10, 0, 0), Color.RED)
	# Z Axis (Blue) - MU-Y (Godot -Z)
	_create_gizmo_line(Vector3.ZERO, Vector3(0, 0, -10), Color.BLUE)
	# Y Axis (Green) - MU-Z (Godot +Y)
	_create_gizmo_line(Vector3.ZERO, Vector3(0, 10, 0), Color.GREEN)

func _create_gizmo_line(start: Vector3, end: Vector3, color: Color):
	var mesh_instance = MeshInstance3D.new()
	var immediate_mesh = ImmediateMesh.new()
	var material = StandardMaterial3D.new()

	mesh_instance.mesh = immediate_mesh
	mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF

	immediate_mesh.surface_begin(Mesh.PRIMITIVE_LINES, material)
	immediate_mesh.surface_add_vertex(start)
	immediate_mesh.surface_add_vertex(end)
	immediate_mesh.surface_end()

	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	material.no_depth_test = true
	_gizmo.add_child(mesh_instance)


func _setup_ui():
	var canvas = CanvasLayer.new()
	add_child(canvas)

	_selector = OptionButton.new()
	_selector.position = Vector2(10, 10)
	_selector.custom_minimum_size = Vector2(280, 0)
	_selector.add_item("Select Model...")
	canvas.add_child(_selector)

	# Sim HUD for coordinate mapping
	_sim_hud = Label.new()
	_sim_hud.position = Vector2(10, 50)
	_sim_hud.text = "Simulation Ready"
	_sim_hud.add_theme_color_override("font_color", Color.YELLOW)
	_sim_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_sim_hud.add_theme_constant_override("outline_size", 4)
	canvas.add_child(_sim_hud)

	# Scan for OBJ files
	var scan_dirs = [
		"res://assets/models",
	]
	for dir_path in scan_dirs:
		_scan_obj_files(dir_path)

	_model_paths.sort()
	for path in _model_paths:
		var label = path.get_file().get_basename()
		var idx = _selector.item_count
		_selector.add_item(label)
		_selector.set_item_metadata(idx, path)

	_selector.item_selected.connect(_on_model_selected)


func _scan_obj_files(dir_path: String):
	var dir = DirAccess.open(dir_path)
	if not dir:
		return
	dir.list_dir_begin()
	var name = dir.get_next()
	while name != "":
		var full = dir_path.path_join(name)
		if dir.current_is_dir() and not name.begins_with("."):
			_scan_obj_files(full)
		elif name.to_lower().ends_with(".obj"):
			_model_paths.append(full)
		name = dir.get_next()


func _on_model_selected(index: int):
	if index == 0:
		for child in model_root.get_children():
			child.queue_free()
		return
	var path = _selector.get_item_metadata(index)
	load_model(path)


func load_model(obj_path: String):
	print("[ModelViewer] Loading: ", obj_path)
	for child in model_root.get_children():
		child.queue_free()

	var mi = MUObjLoader.build_mesh_instance(obj_path)
	if mi:
		model_root.add_child(mi)
		_frame_model()
		_log_health(mi, obj_path)
	else:
		push_warning("[ModelViewer] Failed to load: %s" % obj_path)


func _log_health(mi: MeshInstance3D, path: String):
	var mesh = mi.mesh
	if not mesh:
		print("[ModelViewer] ✗ UNHEALTHY — no mesh data")
		return

	var total_verts = 0
	for s in range(mesh.get_surface_count()):
		var arr = mesh.surface_get_arrays(s)
		if arr and arr[Mesh.ARRAY_VERTEX]:
			total_verts += arr[Mesh.ARRAY_VERTEX].size()

	var mat = mi.get_active_material(0)
	var has_tex = false
	if mat is BaseMaterial3D and mat.albedo_texture:
		has_tex = true

	var status = "✓ HEALTHY" if total_verts > 0 else "✗ EMPTY"
	print("[ModelViewer] %s — %s" % [status, path.get_file()])
	print("  Surfaces: %d | Vertices: %d | Texture: %s" % [
		mesh.get_surface_count(), total_verts,
		"yes" if has_tex else "none"
	])


func _frame_model():
	## Center orbit on model AABB and set distance to frame it.
	var aabb = _get_aabb(model_root)
	if aabb.size.length() == 0:
		print("[ModelViewer] AABB is empty!")
		return

	print("[ModelViewer] AABB: %s size=%s" % [aabb, aabb.size])
	_orbit_center = aabb.get_center()
	var max_dim = max(aabb.size.x, max(aabb.size.y, aabb.size.z))

	# Distance to frame the model with some padding
	var fov_rad = deg_to_rad(camera.fov)
	var dist = (max_dim / 2.0) / tan(fov_rad / 2.0) * 1.5
	_target_distance = max(dist, 0.5)
	_distance = _target_distance

	# Scale zoom limits to model size
	_min_distance = max_dim * 0.3
	_max_distance = max_dim * 10.0

	# Reset to default viewing angle
	_target_yaw = 45.0
	_target_pitch = -30.0
	_yaw = _target_yaw
	_pitch = _target_pitch

	_apply_camera()
	print(
		"[ModelViewer] Framed: center=%s dist=%.1f"
		% [_orbit_center, _distance]
	)


func _apply_camera():
	orbit_node.position = _orbit_center
	orbit_node.rotation = Vector3(
		deg_to_rad(_pitch), deg_to_rad(_yaw), 0
	)
	camera.position = Vector3(0, 0, _distance)
	camera.rotation = Vector3.ZERO


func _process(delta):
	# Update Simulation HUD with Camera info (mapped to MU space)
	if _sim_hud:
		var cam_pos = camera.global_position
		# Mapping Godot back to MU: Godot(x, y, -z) / 0.01 -> MU(x, z, y)
		var mu_x = cam_pos.x / 0.01
		var mu_y = -cam_pos.z / 0.01
		var mu_z = cam_pos.y / 0.01
		var hud_text = "MU Simulation Pos: (%.1f, %.1f, %.1f)\n" % [mu_x, mu_y, mu_z]
		hud_text += "Godot Pos: %s\nCamera Dist: %.1f" % [cam_pos, _distance]
		_sim_hud.text = hud_text

	# Smooth interpolation
	_yaw = lerp_angle(
		deg_to_rad(_yaw), deg_to_rad(_target_yaw),
		10.0 * delta
	)
	_yaw = rad_to_deg(_yaw)
	_pitch = lerp(_pitch, _target_pitch, 10.0 * delta)
	_distance = lerp(_distance, _target_distance, 10.0 * delta)
	_apply_camera()


func _get_aabb(node: Node3D) -> AABB:
	var aabb = AABB()
	for child in node.get_children():
		if child is MeshInstance3D:
			var m_aabb = child.get_aabb()
			if aabb.size.length() == 0:
				aabb = m_aabb
			else:
				aabb = aabb.merge(m_aabb)
	return aabb


# --- Input ---

func _input(event):
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_P:
			_take_screenshot("res://viewer_manual.png")

	if event is InputEventMouseMotion:
		if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
			_target_yaw -= event.relative.x * 0.3
			_target_pitch -= event.relative.y * 0.3
			_target_pitch = clamp(
				_target_pitch, -89.0, 89.0
			)

	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_target_distance = clamp(
				_target_distance * 0.9,
				_min_distance, _max_distance
			)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_target_distance = clamp(
				_target_distance * 1.1,
				_min_distance, _max_distance
			)


func _take_screenshot(path: String = "res://viewer_capture.png"):
	if DisplayServer.get_name() == "headless":
		print("[ModelViewer] Skipping screenshot in headless mode.")
		return
		
	# Wait for frame to finish rendering
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	
	var img = get_viewport().get_texture().get_image()
	if img:
		var full_path = ProjectSettings.globalize_path(path)
		img.save_png(full_path)
		print("[ModelViewer] Screenshot saved: ", full_path)
	else:
		push_warning("[ModelViewer] Failed to capture viewport texture.")
