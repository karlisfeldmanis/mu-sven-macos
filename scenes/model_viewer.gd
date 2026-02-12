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
var _show_normals: bool = true
var _show_skin: bool = true
var _show_textures: bool = true
var _center_model_enabled: bool = true
var _show_bone_labels: bool = false
var _current_model_path: String = ""
var _model_selector: OptionButton
var _texture_checkbox: CheckBox
var _pose_selector: OptionButton
var _current_pose_idx: int = 0
var _animated_materials: Array[Dictionary] = [] # Stores {material: Material, speed: Vector2}
var _debug_nodes: Array[Node] = []

const MUMaterialHelper = preload("res://addons/mu_tools/rendering/material_helper.gd")
const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd")
const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")
const MUObjLoader = preload("res://addons/mu_tools/parsers/obj_loader.gd")
const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")

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
		if args[i] == "--no-labels":
			_show_bone_labels = false
	
	if not model_to_load.is_empty():
		# Resolve short names (Object1/Stone01) to full paths
		if not model_to_load.begins_with("res://"):
			var candidates = MUModelRegistry.get_all_available_models()
			for c in candidates:
				if model_to_load in c:
					model_to_load = c
					break
		
		_model_container.scale = Vector3(model_scale, model_scale, model_scale)
		_current_pose_idx = MUModelRegistry.get_bind_pose_action(model_to_load, 0)
		load_model(model_to_load, no_skin, !_show_textures)
		
		# Update UI selection to match
		for idx in range(_model_selector.item_count):
			if _model_selector.get_item_metadata(idx) == model_to_load:
				_model_selector.select(idx)
				break
		
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
	
	# Texture Toggle
	_texture_checkbox = CheckBox.new()
	_texture_checkbox.text = "Show Textures"
	_texture_checkbox.button_pressed = _show_textures
	_texture_checkbox.toggled.connect(_on_texture_toggled)
	container.add_child(_texture_checkbox)
	
	# Bind Pose Selector
	var pose_container = HBoxContainer.new()
	container.add_child(pose_container)
	
	var pose_label = Label.new()
	pose_label.text = "Bind Pose: "
	pose_container.add_child(pose_label)
	
	_pose_selector = OptionButton.new()
	for i in range(10): # Initial buffer, will be updated on load
		_pose_selector.add_item("Action %d" % i, i)
	_pose_selector.item_selected.connect(_on_pose_selected)
	pose_container.add_child(_pose_selector)
	
	# 2. Automated Model Discovery (Centralized via Registry)
	var models = MUModelRegistry.get_all_available_models()
	
	_model_selector.add_item("Select Model...", -1)
	for path in models:
		# Show folder prefix for clarity (e.g. "Object1/Stone01")
		var folder = path.get_base_dir().get_file()
		var display_name = folder + "/" + path.get_file().get_basename()
		_model_selector.add_item(display_name)
		_model_selector.set_item_metadata(_model_selector.get_item_count() - 1, path)
			
		# Select current model if applicable
		if not _current_model_path.is_empty() and path == _current_model_path:
			_model_selector.select(_model_selector.get_item_count() - 1)

func _on_model_selected(index):
	print("[Debugger] Item Selected: ", index)
	if not _model_selector:
		print("❌ _model_selector is null!")
		return
		
	if index > 0: # 0 is "Select Model..."
		var path = _model_selector.get_item_metadata(index)
		print("[Debugger] Selected Model Path (from metadata): ", path)
		_current_pose_idx = MUModelRegistry.get_bind_pose_action(path, 0)
		_update_pose_selector_count(path)
		load_model(path, !_show_skin, !_show_textures)

func _on_texture_toggled(pressed: bool):
	_show_textures = pressed
	if not _current_model_path.is_empty():
		load_model(_current_model_path, !_show_skin, !_show_textures)

func _on_pose_selected(index: int):
	_current_pose_idx = index
	if not _current_model_path.is_empty():
		load_model(_current_model_path, !_show_skin, !_show_textures)

func _update_pose_selector_count(path: String):
	if not path.ends_with(".bmd"):
		return
	
	var parser = BMDParser.new()
	if parser.parse_file(path, false):
		_pose_selector.clear()
		for i in range(parser.actions.size()):
			_pose_selector.add_item("Action %d" % i, i)
		if _current_pose_idx < _pose_selector.item_count:
			_pose_selector.select(_current_pose_idx)

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
	
	# No directional light (Sun) for flat "unlit" look, rely on Ambient
	
	# 2. Environment (Neutral Studio -> Matches Main.tscn)
	
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
	sun.light_energy = 1.0
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
	var grid_size = 50
	var step = 1.0
	for i in range(-grid_size, grid_size + 1):
		# Grid lines every 1m
		var color = Color(0.3, 0.3, 0.3)
		if i % 10 == 0: color = Color(0.5, 0.5, 0.5) # Major lines every 10m
		if i == 0: color = Color(0.8, 0.2, 0.2) if i == 0 else color # Origin
		
		st.set_color(color)
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
	container.visible = true # Focus on mesh by default
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
		
	container.add_child(create_line.call(Vector3(1, 0, 0), Color.RED)) # X
	container.add_child(create_line.call(Vector3(0, 1, 0), Color.GREEN)) # Y
	container.add_child(create_line.call(Vector3(0, 0, 1), Color.BLUE)) # Z
	
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
	for n in _debug_nodes:
		if is_instance_valid(n):
			n.queue_free()
	_debug_nodes.clear()
	
	if path.ends_with(".bmd"):
		_load_bmd(path, no_skin, no_texture)
	elif path.ends_with(".obj"):
		_load_obj(path, no_texture)
		
	# Apply optional rotation override from registry
	var rot_override = MUModelRegistry.get_rotation_override(path)
	if rot_override != Vector3.ZERO:
		_model_container.rotation_degrees = rot_override
		print("[Debugger] Applied Rotation Override: ", rot_override)
	else:
		_model_container.rotation = Vector3.ZERO

func _load_bmd(path: String, _no_skin: bool = false, _no_texture: bool = false):
	# Fallback to manual parsing if import plugin isn't active/cached
	var MUMeshBuilderClass = load("res://addons/mu_tools/rendering/bmd_mesh_builder.gd")
	var MUSkeletonBuilderClass = load("res://addons/mu_tools/ui/skeleton_builder.gd")
	
	var parser = BMDParser.new()
	if not parser.parse_file(path, false):
		print("❌ BMD Parse Failed")
		return
		
	var skeleton = MUSkeletonBuilderClass.build_skeleton(parser.bones, parser.actions)
	_model_container.add_child(skeleton)
	
	var hidden_mesh_idx = MUModelRegistry.get_hidden_mesh(path)
	var meta = MUModelRegistry.get_metadata(path)
	var alpha = meta.get("alpha", 1.0)
	
	for i in range(parser.meshes.size()):
		if i == hidden_mesh_idx:
			print("[Debugger] Skipping Hidden Mesh: %d" % i)
			continue
			
		var bmd_mesh = parser.meshes[i]
		# 🟢 UI Support: Use _current_pose_idx instead of hardcoded Action 0
		var mi = MUMeshBuilderClass.create_mesh_instance_v2(
				bmd_mesh, skeleton, path, parser, true, _no_skin, _no_texture, i, _current_pose_idx, _animated_materials)
		if mi:
			var mat = mi.get_active_material(0)
			if mat and mat.get_meta("mu_script_hidden", false):
				print("[Debugger] Skipping Script-Hidden Mesh: %d (%s)" % [i, bmd_mesh.texture_filename])
				mi.free()
				continue
				
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			_model_container.add_child(mi)
			
			# Apply BlendMesh logic (Additive)
			var blend_mesh_idx = MUModelRegistry.get_blend_mesh_index(path)
			if i == blend_mesh_idx:
				if mat is StandardMaterial3D:
					mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
					mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA # Ensure alpha support
					print("[Debugger] Applied Additive Blending to BlendMesh: %d" % i)
			
			# Apply Object Alpha & Light
			var light = meta.get("light", [1.0, 1.0, 1.0])
			
			if alpha < 1.0 or light[0] < 1.0 or light[1] < 1.0 or light[2] < 1.0:
				if mat is StandardMaterial3D:
					if alpha < 1.0:
						mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
					mat.albedo_color = Color(light[0], light[1], light[2], alpha)
			var aabb = mi.mesh.get_aabb()
			print("[Debugger] Mesh %d AABB: %s" % [i, aabb])
	
	# Visualize Skeleton Bones
	if skeleton and _show_bone_labels:
		for j in range(skeleton.get_bone_count()):
			var b_pos = skeleton.get_bone_global_pose(j).origin
			var lbl = Label3D.new()
			lbl.text = "%d: %s" % [j, skeleton.get_bone_name(j)]
			lbl.pixel_size = 0.002
			lbl.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			lbl.position = b_pos
			_model_container.add_child(lbl)
			_debug_nodes.append(lbl)
	
	_center_model()

func _draw_mesh_debug(mi: MeshInstance3D):
	if not mi or not mi.mesh: return
	
	# 1. Bounding Box
	var aabb = mi.mesh.get_aabb()
	var box_instance = MeshInstance3D.new()
	var box_mesh = BoxMesh.new()
	box_mesh.size = aabb.size
	var box_mat = StandardMaterial3D.new()
	box_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	box_mat.albedo_color = Color(1, 1, 0, 0.2)
	box_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	box_instance.mesh = box_mesh
	box_instance.material_override = box_mat
	box_instance.position = aabb.get_center()
	mi.add_child(box_instance)
	_debug_nodes.append(box_instance)
	
	# 2. Normals (Cyan lines)
	if _show_normals:
		var imm = ImmediateMesh.new()
		var mdt = MeshDataTool.new()
		mdt.create_from_surface(mi.mesh, 0)
		
		imm.surface_begin(Mesh.PRIMITIVE_LINES)
		imm.surface_set_color(Color.CYAN)
		for i in range(mdt.get_vertex_count()):
			var v = mdt.get_vertex(i)
			var n = mdt.get_normal(i)
			imm.surface_add_vertex(v)
			imm.surface_add_vertex(v + n * 0.2)
		imm.surface_end()
		
		var normal_mi = MeshInstance3D.new()
		normal_mi.mesh = imm
		var norm_mat = StandardMaterial3D.new()
		norm_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		norm_mat.no_depth_test = true
		norm_mat.vertex_color_use_as_albedo = true
		normal_mi.material_override = norm_mat
		mi.add_child(normal_mi)
		_debug_nodes.append(normal_mi)

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
		print("[Debugger] Mesh '%s' AABB: %s" % [mi.name, global_aabb])
		
		if not started:
			full_aabb = global_aabb
			started = true
		else:
			full_aabb = full_aabb.merge(global_aabb)
	
	if started:
		var center = full_aabb.get_center()
		
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
