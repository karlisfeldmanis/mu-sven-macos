extends Node3D

# Viewport and UI
var _camera: Camera3D
var _camera_pivot: Node3D
var _model_container: Node3D
var _grid: MeshInstance3D

# Camera State
var _yaw: float = 0.0
var _pitch: float = -30.0
var _distance: float = 5.0
var _target_yaw: float = 0.0
var _target_pitch: float = -30.0
var _target_distance: float = 5.0

# Debug State
var _show_wireframe: bool = false
var _show_normals: bool = false
var _show_skin: bool = true
var _show_textures: bool = true
var _center_model_enabled: bool = true
var _current_model_path: String = ""
var _model_selector: OptionButton
var _animated_materials: Array[Dictionary] = [] # Stores {material: Material, speed: Vector2}

const MUMaterialHelper = preload("res://addons/mu_tools/core/mu_material_helper.gd")
const MUModelRegistry = preload("res://addons/mu_tools/core/mu_model_registry.gd")
const MUObjLoader = preload("res://addons/mu_tools/core/mu_obj_loader.gd")

func _ready():
	_setup_scene()
	_setup_ui()
	_setup_axis_marker()
	
	# Parse CLI Arguments
	var args = OS.get_cmdline_args()
	var model_to_load = ""
	var screenshot_path = ""
	var model_scale = 1.0
	var no_skin = false
	
	for i in range(args.size()):
		if args[i] == "--model" and i + 1 < args.size():
			model_to_load = args[i + 1]
		if args[i] == "--out" and i + 1 < args.size():
			screenshot_path = args[i + 1]
		if args[i] == "--scale" and i + 1 < args.size():
			model_scale = args[i + 1].to_float()
		if args[i] == "--no-skin":
			no_skin = true
		if args[i] == "--no-texture":
			_show_textures = false
		if args[i] == "--no-center":
			_center_model_enabled = false
	
	if not model_to_load.is_empty():
		_model_container.scale = Vector3(model_scale, model_scale, model_scale)
		load_model(model_to_load, no_skin, !_show_textures)
		
		if not screenshot_path.is_empty():
			# Wait for frames to render and animations to settle
			await get_tree().create_timer(2.0).timeout 
			take_automated_screenshot(screenshot_path)
			await get_tree().process_frame
			get_tree().quit()
	else:
		print("[Debugger] No --model specified. Waiting for manual interaction or command.")

func _setup_ui():
	# UI Layer
	var canvas = CanvasLayer.new()
	add_child(canvas)
	
	# Container for UI
	var container = VBoxContainer.new()
	container.position = Vector2(10, 10)
	canvas.add_child(container)
	
	# Model Selector Dropdown
	_model_selector = OptionButton.new()
	_model_selector.name = "ModelSelector"
	_model_selector.item_selected.connect(_on_model_selected)
	container.add_child(_model_selector)
	
	# Scan for models
	var scan_dirs = ["res://assets/lorencia", "res://extracted_data/object_models"]
	var models = []
	
	for d_path in scan_dirs:
		var dir = DirAccess.open(d_path)
		if dir:
			dir.list_dir_begin()
			var file_name = dir.get_next()
			while file_name != "":
				if not dir.current_is_dir() and (file_name.ends_with(".obj") or file_name.ends_with(".bmd")):
					# Store full path or relative? 
					# Storing full path is safer if we have multiple source dirs
					models.append(d_path.path_join(file_name))
				file_name = dir.get_next()
		
	models.sort()
	
	_model_selector.add_item("Select Model...", -1)
	for model in models:
		_model_selector.add_item(model)
			
		# Select current model if applicable
		if not _current_model_path.is_empty():
			for i in range(_model_selector.item_count):
				if _model_selector.get_item_text(i) == _current_model_path:
					_model_selector.select(i)
					break

func _on_model_selected(index):
	print("[Debugger] Item Selected: ", index)
	if not _model_selector:
		print("❌ _model_selector is null!")
		return
		
	if index > 0: # 0 is "Select Model..."
		var path = _model_selector.get_item_text(index)
		print("[Debugger] Selected Model Path: ", path)
		load_model(path, !_show_skin, !_show_textures)

func _setup_scene():
	# Pivot for orbital camera
	_camera_pivot = Node3D.new()
	_camera_pivot.name = "CameraPivot"
	add_child(_camera_pivot)
	
	_camera = Camera3D.new()
	_camera.name = "MainCamera"
	_camera.current = true
	_camera.position = Vector3(0, 0, _distance)
	_camera_pivot.add_child(_camera)
	
	_model_container = Node3D.new()
	_model_container.name = "ModelContainer"
	add_child(_model_container)
	
	# Reference Grid
	_grid = MeshInstance3D.new()
	var grid_mesh = PlaneMesh.new()
	_update_camera()

	# --- Lighting & Environment (Neutral/Flat White) ---
	
	# No directional light (Sun) for flat "unlit" look, rely on Ambient
	
	# 2. Environment (Neutral Studio -> Matches Main.tscn)
	var env = Environment.new()
	
	# Disable Fog/Glow
	env.fog_enabled = false
	env.glow_enabled = false
	
	# Background & Ambient
	get_viewport().transparent_bg = false # Disabled for visibility
	
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.2, 0.2, 0.25, 1) # Matches main.tscn
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.1, 0.1, 0.1, 1) # Matches main.tscn
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	
	var world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)
	
	# Directional Light (Matches main.tscn)
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.light_energy = 1.2
	sun.shadow_enabled = false
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.transform = Transform3D(
		Vector3(0.866025, -0.433013, 0.25),
		Vector3(0, 0.5, 0.866025),
		Vector3(-0.5, -0.75, 0.433013),
		Vector3(0, 3, 2)
	)
	add_child(sun)

	# --- Grid (Visual Helper) ---
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_LINES)
	var grid_size = 10
	var step = 10.0
	for i in range(-grid_size, grid_size + 1):
		# LINES
		st.set_color(Color(0.3, 0.3, 0.3))
		st.add_vertex(Vector3(i * step, 0, -grid_size * step))
		st.add_vertex(Vector3(i * step, 0, grid_size * step))
		st.add_vertex(Vector3(-grid_size * step, 0, i * step))
		st.add_vertex(Vector3(grid_size * step, 0, i * step))
		
	_grid = MeshInstance3D.new()
	_grid.mesh = st.commit()
	var mat = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	_grid.material_override = mat
	_grid.visible = true
	add_child(_grid)

func _update_camera():
	_camera.current = true
	_camera.position = Vector3(0, 0, _distance)

func _process(delta):
	var args_list = Array(OS.get_cmdline_args())
	var is_screenshot = not args_list.filter(func(a): return a.begins_with("--out")).is_empty()
	
	if is_screenshot:
		# Static camera for reliable centering
		_camera_pivot.position = Vector3.ZERO
		_camera_pivot.rotation = Vector3.ZERO
		# Position camera at a 45/45 degree angle for consistency
		var view_dist = _distance * 1.5
		_camera.position = Vector3(view_dist * 0.7, view_dist * 0.7, view_dist * 0.7)
		_camera.look_at(Vector3.ZERO)
	else:
		# Orbital Controls
		_yaw = lerp_angle(_yaw, deg_to_rad(_target_yaw), 10.0 * delta)
		_pitch = lerp(_pitch, _target_pitch, 10.0 * delta)
		_distance = lerp(_distance, _target_distance, 10.0 * delta)
		
		_camera_pivot.rotation.y = _yaw
		_camera_pivot.rotation.x = deg_to_rad(_pitch)
		_camera.position.z = _distance
		_camera.look_at(_camera_pivot.global_position, _camera_pivot.global_transform.basis.y)

	# Animate Materials
	if _animated_materials.size() > 0:
		var time = Time.get_ticks_msec() / 1000.0
		for item in _animated_materials:
			var mat = item["material"] as BaseMaterial3D
			var speed = item["speed"] as Vector2
			if mat:
				mat.uv1_offset = Vector3(time * speed.x, time * speed.y, 0)
				# print("[Debugger] Animating Material: ", mat.resource_name, " UV: ", mat.uv1_offset)

func _input(event):
	if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		_target_yaw -= event.relative.x * 0.5
		_target_pitch -= event.relative.y * 0.5
		_target_pitch = clamp(_target_pitch, -89.0, 89.0)
		
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_target_distance = max(0.5, _target_distance - 0.5)
		if event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_target_distance += 0.5
			
	if event is InputEventKey and event.pressed:
		match event.keycode:
			KEY_W: toggle_wireframe()
			KEY_N: toggle_normals()
			KEY_O: _open_file_dialog("*.obj ; OBJ Models")
			KEY_B: _open_file_dialog("*.bmd ; BMD Models")
			KEY_G: _grid.visible = !_grid.visible
			KEY_S: toggle_skinning()
			KEY_T: toggle_textures()
			KEY_X: 
				var axis = get_node_or_null("AxisMarker")
				if axis: axis.visible = !axis.visible

func toggle_wireframe():
	_show_wireframe = !_show_wireframe
	for mesh in _get_all_meshes(_model_container):
		var mat = mesh.get_active_material(0)
		if mat is StandardMaterial3D or mat is ShaderMaterial:
			# Shaders might need internal uniform, but for Standard we can cheat
			if mat is StandardMaterial3D:
				var mode = BaseMaterial3D.SHADING_MODE_UNSHADED if _show_wireframe \
						else BaseMaterial3D.SHADING_MODE_PER_PIXEL
				mat.shading_mode = mode
	print("[Debugger] Wireframe: ", _show_wireframe)

func toggle_normals():
	_show_normals = !_show_normals
	var container = get_node_or_null("NormalDebug")
	if container:
		container.visible = _show_normals
	elif _show_normals:
		_create_normal_visualization()
	print("[Debugger] Normals: ", _show_normals)

func toggle_skinning():
	_show_skin = !_show_skin
	for child in _model_container.get_children():
		if child is Skeleton3D:
			child.visible = _show_skin
			# If we hide the skeleton, meshes usually disappear if they are children
			# So we just disable the skin property on MeshInstances
			for mesh_instance in _get_all_meshes(child):
				if not _show_skin:
					mesh_instance.skin = null
				else:
					# Force a reload or store the skin? 
					# Simpler: just reload model without skin if toggled?
					pass 
	print("[Debugger] Skinning: ", _show_skin)
	load_model(_current_model_path, !_show_skin, !_show_textures)

func toggle_textures():
	_show_textures = !_show_textures
	print("[Debugger] Textures: ", _show_textures)
	load_model(_current_model_path, !_show_skin, !_show_textures)

func _create_normal_visualization():
	var container = Node3D.new()
	container.name = "NormalDebug"
	add_child(container)
	
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color.CYAN
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	
	for mesh_instance in _get_all_meshes(_model_container):
		if not mesh_instance.mesh: continue
		
		var st = SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_LINES)
		
		for s in range(mesh_instance.mesh.get_surface_count()):
			var mdata = MeshDataTool.new()
			mdata.create_from_surface(mesh_instance.mesh, s)
			
			for i in range(mdata.get_vertex_count()):
				var p = mdata.get_vertex(i)
				var n = mdata.get_vertex_normal(i)
				st.add_vertex(p)
				st.add_vertex(p + n * 0.1) # Shorter normals for better density
		
		var lines = MeshInstance3D.new()
		lines.mesh = st.commit()
		lines.material_override = mat
		lines.transform = mesh_instance.global_transform
		container.add_child(lines)

func _setup_axis_marker():
	var container = Node3D.new()
	container.name = "AxisMarker"
	container.visible = false # Focus on mesh by default
	add_child(container)
	
	var create_line = func(to: Vector3, color: Color):
		var st = SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_LINES)
		st.add_vertex(Vector3.ZERO)
		st.add_vertex(to)
		var mi = MeshInstance3D.new()
		mi.mesh = st.commit()
		var mat = StandardMaterial3D.new()
		mat.albedo_color = color
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mi.material_override = mat
		return mi
		
	container.add_child(create_line.call(Vector3(1, 0, 0), Color.RED))   # X
	container.add_child(create_line.call(Vector3(0, 1, 0), Color.GREEN)) # Y
	container.add_child(create_line.call(Vector3(0, 0, 1), Color.BLUE))  # Z
	
	# Add negative axes for completeness
	container.add_child(create_line.call(Vector3(-1, 0, 0), Color.MAROON))
	container.add_child(create_line.call(Vector3(0, -1, 0), Color.DARK_GREEN))
	container.add_child(create_line.call(Vector3(0, 0, -1), Color.DARK_BLUE))

func _open_file_dialog(_filter: String):
	print("[Debugger] Open File Dialog requested. Filter: ", _filter)

func load_model(path: String, no_skin: bool = false, no_texture: bool = false):
	print("[Debugger] Loading: ", path, " (NoSkin: ", no_skin, ", NoTexture: ", no_texture, ")")
	_current_model_path = path
	
	# Clear old
	for child in _model_container.get_children():
		child.queue_free()
	var old_normals = get_node_or_null("NormalDebug")
	if old_normals: old_normals.queue_free()
	
	if path.ends_with(".bmd"):
		_load_bmd(path, no_skin, no_texture)
	elif path.ends_with(".obj"):
		_load_obj(path, no_texture)

func _load_bmd(path: String, _no_skin: bool = false, _no_texture: bool = false):
	# Try loading as a native Godot resource (verifies the EditorImportPlugin)
	var scene = load(path)
	if scene is PackedScene:
		print("[Debugger] Loaded BMD as Native PackedScene")
		var instance = scene.instantiate()
		_model_container.add_child(instance)
	else:
		# Fallback to manual parsing if import plugin isn't active/cached
		var BMDParserClass = load("res://addons/mu_tools/core/bmd_parser.gd")
		var MUMeshBuilderClass = load("res://addons/mu_tools/nodes/mesh_builder.gd")
		var MUSkeletonBuilderClass = load("res://addons/mu_tools/ui/skeleton_builder.gd")
		
		var parser = BMDParserClass.new()
		if not parser.parse_file(path, false):
			print("❌ BMD Parse Failed")
			return
			
		var skeleton = MUSkeletonBuilderClass.build_skeleton(parser.bones, parser.actions)
		_model_container.add_child(skeleton)
		
		for i in range(parser.meshes.size()):
			var bmd_mesh = parser.meshes[i]
			var mi = MUMeshBuilderClass.create_mesh_instance(
					bmd_mesh, skeleton, path, parser, false, _no_skin, _no_texture)
			if mi:
				mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				skeleton.add_child(mi)
				mi.skeleton = mi.get_path_to(skeleton)
				
				# [Antigravity] Diagnostic Dump
				var aabb = mi.mesh.get_aabb()
				print("[Debugger] Mesh AABB: ", aabb)
				print("[Debugger]   Size: ", aabb.size)
				print("[Debugger]   Center: ", aabb.get_center())
	
	_center_model()

func _load_obj(path: String, no_texture: bool = false):
	_animated_materials.clear()

	var mi = MUObjLoader.build_mesh_instance(path, _animated_materials)
	if not mi:
		push_warning("[Debugger] Failed to load OBJ: %s" % path)
		return

	if no_texture:
		var mat = StandardMaterial3D.new()
		mat.albedo_color = Color.GRAY
		mat.cull_mode = BaseMaterial3D.CULL_DISABLED
		mi.material_override = mat

	_model_container.add_child(mi)

	if mi.mesh:
		var aabb = mi.mesh.get_aabb()
		print("[Debugger] OBJ AABB: ", aabb, " Size: ", aabb.size)

	_center_model()

func _center_model():
	if not _center_model_enabled:
		print("[Debugger] Skipping Auto-Center (--no-center used)")
		return
	var full_aabb: AABB
	var started = false
	
	var meshes = _get_all_meshes(_model_container)
	for mi in meshes:
		if not mi.mesh: continue
		
		# Get local AABB from mesh (covers all surfaces)
		var local_aabb = mi.mesh.get_aabb()
		# Transform local AABB to world space based on the mesh instance's transform
		var global_aabb = mi.global_transform * local_aabb
		
		if not started:
			full_aabb = global_aabb
			started = true
		else:
			full_aabb = full_aabb.merge(global_aabb)
	
	if started:
		var center = full_aabb.get_center()
		
		# 1. Normalize Model Position: Move children to origin
		# This ensures the model is centered on the origin grid/axes.
		for child in _model_container.get_children():
			child.position -= center / _model_container.scale
		
		# Reset camera pivot to origin
		_camera_pivot.position = Vector3.ZERO
		
		# 2. Calculate Framing Distance based on AABB size
		var size = full_aabb.size
		var max_dim = max(size.x, max(size.y, size.z))
		
		var fov_rad = deg_to_rad(_camera.fov)
		# Ensure we don't have a tiny FOV
		if _camera.fov < 1.0: _camera.fov = 75.0
		
		var dist = (max_dim / 2.0) / tan(fov_rad / 2.0)
		
		# Add a bit of space (20% buffer)
		dist *= 1.2
		_target_distance = max(dist, 0.5)
		_distance = _target_distance
		
		# 3. Default isometric view
		_target_pitch = -30.0
		_target_yaw = 45.0
		_pitch = _target_pitch
		_yaw = deg_to_rad(_target_yaw)
		
		_camera.near = 0.001
		_camera.far = 10000.0
		
		print("[Debugger] Auto-Centered. AABB Center: %s, Max Dim: %.2f, Distance: %.2f" % [center, max_dim, _target_distance])
	else:
		# Fallback: Reset pivot to origin
		_camera_pivot.position = Vector3.ZERO
		_target_distance = 5.0
		_distance = 5.0
		print("[Debugger] No meshes found to center on.")

func take_automated_screenshot(path: String):
	print("[Debugger] Taking Automated Screenshot to: ", path)
	var viewport = get_viewport()
	if DisplayServer.get_name() == "headless":
		print("⚠️ Cannot take screenshot in headless mode (Renderer is Dummy).")
		return
		
	var texture = viewport.get_texture()
	if not texture:
		print("❌ Viewport texture is null.")
		return
		
	var img = texture.get_image()
	if img:
		img.save_png(path)
		print("✓ Screenshot saved successfully.")
	else:
		print("❌ Failed to capture image from texture.")

func _get_all_meshes(node: Node) -> Array[MeshInstance3D]:
	var results: Array[MeshInstance3D] = []
	var stack = [node]
	while stack.size() > 0:
		var n = stack.pop_back()
		if n.is_queued_for_deletion():
			continue
		if n is MeshInstance3D and n.mesh:
			results.append(n)
		for child in n.get_children():
			stack.append(child)
	return results
