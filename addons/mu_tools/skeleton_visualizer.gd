@tool
class_name MUSkeletonVisualizer
extends Node3D

## Skeleton Visualizer Tool
## Displays bone hierarchy, positions, and orientations for debugging

@export var skeleton: Skeleton3D:
	set(value):
		skeleton = value
		if Engine.is_editor_hint():
			_update_visualization()

@export var show_bone_names: bool = true:
	set(value):
		show_bone_names = value
		if Engine.is_editor_hint():
			_update_visualization()

@export var bone_size: float = 0.05:
	set(value):
		bone_size = value
		if Engine.is_editor_hint():
			_update_visualization()

@export var line_thickness: float = 0.01:
	set(value):
		line_thickness = value
		if Engine.is_editor_hint():
			_update_visualization()

@export_color_no_alpha var bone_color: Color = Color.GREEN:
	set(value):
		bone_color = value
		if Engine.is_editor_hint():
			_update_visualization()

@export_color_no_alpha var connection_color: Color = Color.CYAN:
	set(value):
		connection_color = value
		if Engine.is_editor_hint():
			_update_visualization()

var _bone_spheres: Dictionary = {} # bone_idx -> MeshInstance3D
var _bone_connections: Dictionary = {} # bone_idx -> MeshInstance3D
var _labels: Array[Label3D] = []

func _ready() -> void:
	_update_visualization()

func _process(_delta: float) -> void:
	if not skeleton: return
	_update_live_positions()

func _update_live_positions() -> void:
	for bone_idx in _bone_spheres.keys():
		var sphere = _bone_spheres[bone_idx]
		var bone_pose = skeleton.get_bone_global_pose(bone_idx)
		sphere.position = bone_pose.origin
		
		if _bone_connections.has(bone_idx):
			var connection = _bone_connections[bone_idx]
			var parent_idx = skeleton.get_bone_parent(bone_idx)
			var parent_pose = skeleton.get_bone_global_pose(parent_idx)
			_update_connection_transform(connection, bone_pose.origin, parent_pose.origin)

func _update_visualization() -> void:
	_clear_visualization()
	_bone_spheres.clear()
	_bone_connections.clear()
	
	if not skeleton: return
	
	for bone_idx in range(skeleton.get_bone_count()):
		_visualize_bone(bone_idx)

func _visualize_bone(bone_idx: int) -> void:
	var bone_name = skeleton.get_bone_name(bone_idx)
	var bone_pose = skeleton.get_bone_global_rest(bone_idx)
	var bone_pos = bone_pose.origin
	
	# Create sphere at bone position
	var sphere = _create_sphere(bone_pos, bone_size, bone_color)
	sphere.name = "Bone_%d_%s" % [bone_idx, bone_name]
	add_child(sphere)
	_bone_spheres[bone_idx] = sphere
	
	# Add bone name label
	if show_bone_names:
		var label = Label3D.new()
		label.text = bone_name
		label.position = bone_pos + Vector3(0, bone_size * 2, 0)
		label.pixel_size = 0.001
		label.modulate = Color.WHITE
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.no_depth_test = true
		add_child(label)
		_labels.append(label)
	
	# Draw connection to parent
	var parent_idx = skeleton.get_bone_parent(bone_idx)
	if parent_idx >= 0:
		var parent_pose = skeleton.get_bone_global_rest(parent_idx)
		var parent_pos = parent_pose.origin
		
		var connection = _create_bone_connection(bone_pos, parent_pos)
		add_child(connection)
		_bone_connections[bone_idx] = connection
	
	# Draw bone orientation axes (Disabled for now to avoid complexity in live update)
	# var axes = _create_axes(bone_pose, bone_size * 2)
	# add_child(axes)

func _create_sphere(pos: Vector3, radius: float, color: Color) -> MeshInstance3D:
	var mesh_instance = MeshInstance3D.new()
	var sphere_mesh = SphereMesh.new()
	sphere_mesh.radius = radius
	sphere_mesh.height = radius * 2
	mesh_instance.mesh = sphere_mesh
	
	var material = StandardMaterial3D.new()
	material.albedo_color = color
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.disable_receive_shadows = true
	material.no_depth_test = true
	mesh_instance.material_override = material
	
	mesh_instance.position = pos
	return mesh_instance

func _create_bone_connection(from: Vector3, to: Vector3) -> MeshInstance3D:
	var connection = MeshInstance3D.new()
	var cylinder = CylinderMesh.new()
	cylinder.top_radius = line_thickness
	cylinder.bottom_radius = line_thickness
	cylinder.height = 1.0 # Standard height for scaling
	connection.mesh = cylinder
	
	var material = StandardMaterial3D.new()
	material.albedo_color = connection_color
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.disable_receive_shadows = true
	material.no_depth_test = true
	connection.material_override = material
	
	_update_connection_transform(connection, from, to)
	return connection

func _update_connection_transform(connection: MeshInstance3D, from: Vector3, to: Vector3) -> void:
	var direction = to - from
	var length = direction.length()
	var midpoint = (from + to) / 2.0
	
	# Reset transform
	connection.transform = Transform3D.IDENTITY
	connection.position = midpoint
	
	if length > 0.0001:
		var up = Vector3.UP
		var direction_norm = direction.normalized()
		var axis = up.cross(direction_norm)
		if axis.length() > 0.0001:
			var angle = up.angle_to(direction_norm)
			connection.rotate(axis.normalized(), angle)
		elif direction.y < 0:
			connection.rotate(Vector3.RIGHT, PI)
		
		# Scale the Y (height) of the cylinder to match length
		if connection.mesh is CylinderMesh:
			var cyl = connection.mesh as CylinderMesh
			connection.scale.y = length / cyl.height

func _create_axes(transform: Transform3D, length: float) -> Node3D:
	var axes_root = Node3D.new()
	axes_root.transform = transform
	
	# X axis (Red)
	var x_axis = _create_axis_arrow(Vector3.RIGHT * length, Color.RED)
	axes_root.add_child(x_axis)
	
	# Y axis (Green)
	var y_axis = _create_axis_arrow(Vector3.UP * length, Color.GREEN)
	axes_root.add_child(y_axis)
	
	# Z axis (Blue)
	var z_axis = _create_axis_arrow(Vector3.BACK * length, Color.BLUE)
	axes_root.add_child(z_axis)
	
	return axes_root

func _create_axis_arrow(direction: Vector3, color: Color) -> MeshInstance3D:
	var mesh_instance = MeshInstance3D.new()
	var cylinder = CylinderMesh.new()
	
	var length = direction.length()
	cylinder.top_radius = length * 0.05
	cylinder.bottom_radius = length * 0.05
	cylinder.height = length
	
	mesh_instance.mesh = cylinder
	mesh_instance.position = direction / 2.0
	
	# Orient towards direction
	var up = Vector3.UP
	var axis = up.cross(direction.normalized())
	if axis.length() > 0.0001:
		var angle = up.angle_to(direction.normalized())
		mesh_instance.rotate(axis.normalized(), angle)
	elif direction.y < 0:
		mesh_instance.rotate(Vector3.RIGHT, PI)
	
	var material = StandardMaterial3D.new()
	material.albedo_color = color
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color.a = 0.7
	material.no_depth_test = true
	mesh_instance.material_override = material
	
	return mesh_instance

func _clear_visualization() -> void:
	for node in _bone_spheres.values():
		if is_instance_valid(node): node.queue_free()
	_bone_spheres.clear()
	
	for node in _bone_connections.values():
		if is_instance_valid(node): node.queue_free()
	_bone_connections.clear()
	
	for label in _labels:
		if is_instance_valid(label):
			label.queue_free()
	_labels.clear()

func print_skeleton_info() -> void:
	if not skeleton:
		print("[Skeleton Visualizer] No skeleton assigned")
		return
	
	print("\n=== SKELETON STRUCTURE ===")
	print("Total bones: %d" % skeleton.get_bone_count())
	print("\nBone Hierarchy:")
	
	for bone_idx in range(skeleton.get_bone_count()):
		var bone_name = skeleton.get_bone_name(bone_idx)
		var parent_idx = skeleton.get_bone_parent(bone_idx)
		var parent_name = skeleton.get_bone_name(parent_idx) if parent_idx >= 0 else "ROOT"
		var rest = skeleton.get_bone_rest(bone_idx)
		var global_rest = skeleton.get_bone_global_rest(bone_idx)
		
		var indent = "  "
		var depth = 0
		var check_idx = parent_idx
		while check_idx >= 0:
			depth += 1
			check_idx = skeleton.get_bone_parent(check_idx)
		
		print("%s[%d] %s (parent: %s)" % [indent.repeat(depth), bone_idx, bone_name, parent_name])
		print("%s    Local Pos: %s" % [indent.repeat(depth), rest.origin])
		print("%s    Global Pos: %s" % [indent.repeat(depth), global_rest.origin])
