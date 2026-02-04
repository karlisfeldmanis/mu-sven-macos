@tool
class_name MUOrientationDebugger
extends Node3D

@export var debug_visibility_test: bool = true

func _init() -> void:
	if Engine.is_editor_hint():
		print("[MU Orientation Debugger] Tool script initialized in Editor")

@export_group("Tavern Finder")
@export var load_tavern: bool = false:
	set(val):
		if val:
			var path = "res://reference/MuMain/src/bin/Data/Object1/House03.bmd"
			self.bmd_file = path
			select_house = "House03"

@export_enum("None", "House01", "House02", "House03", "House04", "House05", 
	"HouseEtc01", "HouseEtc02", "HouseEtc03", "Straw01", "Straw02") \
var select_house: String = "None":
	set(val):
		select_house = val
		if val != "None":
			var path = "res://reference/MuMain/src/bin/Data/Object1/" + val + ".bmd"
			self.bmd_file = path

@export_file("*.bmd") \
var bmd_file: String = "res://reference/MuMain/src/bin/Data/Object1/Bridge01.bmd":
	set(val):
		bmd_file = val
		if is_inside_tree():
			_reload_model()

@export_group("Settings")
@export var bake_pose: bool = true:
	set(val):
		bake_pose = val
		if is_inside_tree():
			_reload_model()

@export var force_refresh: bool = false:
	set(val):
		if is_inside_tree():
			_reload_model()

func _ready() -> void:
	_setup_environment()
	_setup_ui()
	_reload_model()

func _setup_ui() -> void:
	if Engine.is_editor_hint():
		return
		
	var canvas = CanvasLayer.new()
	add_child(canvas)
	
	var scroll = ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(200, 400)
	scroll.position = Vector2(20, 20)
	canvas.add_child(scroll)
	
	var vbox = VBoxContainer.new()
	scroll.add_child(vbox)
	
	var line_edit = LineEdit.new()
	line_edit.placeholder_text = "Enter BMD filename..."
	line_edit.text_submitted.connect(func(new_text):
		if not new_text.ends_with(".bmd"):
			new_text += ".bmd"
		self.bmd_file = "res://reference/MuMain/src/bin/Data/Object1/" + new_text
	)
	vbox.add_child(line_edit)
	
	var label = Label.new()
	label.text = "Select Model:"
	vbox.add_child(label)
	
	var bake_check = CheckButton.new()
	bake_check.text = "Bake Pose"
	bake_check.button_pressed = self.bake_pose
	bake_check.toggled.connect(func(v): 
		self.bake_pose = v
	)
	vbox.add_child(bake_check)
	
	var objects_dir = "res://reference/MuMain/src/bin/Data/Object1/"
	
	for h in houses:
		var btn = Button.new()
		btn.text = h
		btn.pressed.connect(func(): 
			self.bmd_file = objects_dir + h + ".bmd"
		)
		vbox.add_child(btn)

func _reload_model() -> void:
	# Clean up any existing models
	var existing = get_node_or_null("ModelRoot")
	if existing:
		existing.free()
			
	if bmd_file.is_empty():
		return
		
	print("[Isolate Debug] Loading: ", bmd_file)
	
	var parser = BMDParser.new()
	if not parser.parse_file(bmd_file):
		push_error("[Isolate Debug] FAILED to parse BMD: " + bmd_file)
		return
		
	# Create a container for the model
	var model_root = Node3D.new()
	model_root.name = "ModelRoot"
	add_child(model_root)
	
	# Create a skeleton (even for static objects, helps mesh builder)
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	if skeleton:
		model_root.add_child(skeleton)
	
	# Attach meshes
	for bmd_mesh in parser.meshes:
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, bmd_file, parser, bake_pose)
		if mesh_instance:
			if skeleton:
				skeleton.add_child(mesh_instance)
			else:
				model_root.add_child(mesh_instance)
				
	# Auto-position camera to see the model
	_center_camera(model_root)
	
	# Take a screenshot if headless
	if DisplayServer.get_name() == "headless":
		_take_screenshot("isolate_render_" + bmd_file.get_file().get_basename() + ".png")

func _setup_environment() -> void:
	# Add ground grid
	if not get_node_or_null("GroundGrid"):
		var mesh_instance = MeshInstance3D.new()
		mesh_instance.name = "GroundGrid"
		var plane = PlaneMesh.new()
		plane.size = Vector2(10, 10)
		mesh_instance.mesh = plane
		
		var mat = StandardMaterial3D.new()
		mat.albedo_color = Color(0.2, 0.2, 0.2)
		mat.emission_enabled = true
		mat.emission = Color(0.1, 0.1, 0.1)
		mesh_instance.material_override = mat
		add_child(mesh_instance)
		
	# Add axis indicators
	if not get_node_or_null("Axes"):
		var axes = Node3D.new()
		axes.name = "Axes"
		add_child(axes)
		_create_axis_line(axes, Vector3.RIGHT, Color.RED)    # X
		_create_axis_line(axes, Vector3.UP, Color.GREEN)    # Y
		_create_axis_line(axes, Vector3.FORWARD, Color.BLUE) # Z

	# Add some basic lighting if not present
	if not get_node_or_null("DirectionalLight3D"):
		var sun = DirectionalLight3D.new()
		sun.name = "DirectionalLight3D"
		sun.rotation_degrees = Vector3(-45, 45, 0)
		sun.shadow_enabled = true
		add_child(sun)

func _create_axis_line(parent: Node3D, dir: Vector3, color: Color) -> void:
	var mesh_instance = MeshInstance3D.new()
	var immediate_mesh = ImmediateMesh.new()
	mesh_instance.mesh = immediate_mesh
	
	immediate_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	immediate_mesh.surface_add_vertex(Vector3.ZERO)
	immediate_mesh.surface_add_vertex(dir * 2.0)
	immediate_mesh.surface_end()
	
	var mat = StandardMaterial3D.new()
	mat.shading_mode = StandardMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = color
	mesh_instance.material_override = mat
	parent.add_child(mesh_instance)

func _center_camera(target: Node3D) -> void:
	# Wait for AABB to be valid
	if Engine.is_editor_hint():
		await get_tree().process_frame
		
	var aabb = _get_total_aabb(target)
	var camera = get_node_or_null("Camera3D")
	if camera and not aabb.size.is_zero_approx():
		var center = aabb.get_center()
		var size = aabb.size.length()
		camera.position = center + Vector3(size * 0.5, size * 0.5, size * 1.5)
		camera.look_at(center)
		print("[Isolate Debug] Camera positioned to view AABB: ", aabb)

func _get_total_aabb(node: Node) -> AABB:
	var total_aabb = AABB()
	var first = true
	var stack = [node]
	while not stack.is_empty():
		var n = stack.pop_back()
		if n is MeshInstance3D and n.mesh:
			var mesh_aabb = n.get_aabb()
			var global_aabb = n.global_transform * mesh_aabb
			if first:
				total_aabb = global_aabb
				first = false
			else:
				total_aabb = total_aabb.merge(global_aabb)
		for child in n.get_children():
			stack.append(child)
	return total_aabb

func _take_screenshot(filename: String) -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	var viewport = get_viewport()
	var img = viewport.get_texture().get_image()
	if img:
		img.save_png(filename)
		print("[Isolate Debug] Screenshot saved to: ", filename)
	else:
		push_error("[Isolate Debug] Failed to capture viewport image")

