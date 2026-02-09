extends Node3D

class_name MUCalibrator

var terrain_node: Node3D # MUTerrainSimple
var selected_obj_idx: int = -1
var target_pos: Vector3 = Vector3.ZERO
var debug_mesh: MeshInstance3D

func _ready():
	_setup_debug_mesh()

func setup(terrain: Node3D):
	terrain_node = terrain

func _setup_debug_mesh():
	debug_mesh = MeshInstance3D.new()
	var m = ImmediateMesh.new()
	debug_mesh.mesh = m
	var mat = StandardMaterial3D.new()
	mat.shading_mode = StandardMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	debug_mesh.material_override = mat
	add_child(debug_mesh)

func select_nearest_object(click_pos: Vector3) -> Dictionary:
	if not terrain_node or terrain_node._spawned_objects_parents.is_empty():
		return {}
		
	var min_dist = 10000.0
	var nearest_idx = -1
	
	for i in range(terrain_node._spawned_objects_parents.size()):
		var parent = terrain_node._spawned_objects_parents[i]
		var dist = parent.position.distance_to(click_pos)
		if dist < min_dist:
			min_dist = dist
			nearest_idx = i
			
	if nearest_idx != -1:
		selected_obj_idx = nearest_idx
		var obj = terrain_node._spawned_objects_data[selected_obj_idx]
		var parent = terrain_node._spawned_objects_parents[nearest_idx]
		_draw_selection(parent.position)
		return {
			"index": nearest_idx,
			"type": obj.type,
			"raw_pos": obj.mu_pos_raw,
			"current_pos": parent.position,
			"name": parent.name
		}
		
	return {}

func set_target_position(pos: Vector3):
	target_pos = pos
	# Snap to nearest half-tile center logic?
	# Tile centers are at (x+0.5, y+0.5).
	# But Godot mesh vertices are at integers.
	# If we assume objects are centered on tiles, we want X.5, Z.5
	
	# Visual feedback
	_draw_selection(pos, true)

func calculate_scale_factor(axis_offset: Vector3) -> Dictionary:
	if selected_obj_idx == -1: return {}
	
	var obj_data = terrain_node._spawned_objects_data[selected_obj_idx]
	var raw = obj_data.mu_pos_raw # Vector3(x, y, z) -> Godot(x, z, y) usually
	
	# Godot X = MirrorPivot - (RawX / Scale) + OffsetX
	# (RawX / Scale) = MirrorPivot + OffsetX - GodotX
	# Scale = RawX / (MirrorPivot + OffsetX - GodotX)
	
	# Godot Z = (RawY / Scale) + OffsetZ
	# Scale = RawY / (GodotZ - OffsetZ)
	
	# Target Pos (Godot)
	var gx = target_pos.x
	var gz = target_pos.z
	
	# Inputs
	var pivot_x = terrain_node.mirror_x_pivot
	var off_x = terrain_node.offset_x
	var off_z = terrain_node.offset_z
	
	# Calculate Scale Candidates
	var scale_x = 0.0
	var denom_x = (pivot_x + off_x - gx)
	if abs(denom_x) > 0.001:
		scale_x = raw.x / denom_x
		
	var scale_z = 0.0
	var denom_z = (gz - off_z)
	if abs(denom_z) > 0.001:
		scale_z = raw.y / denom_z # Note: MU Y is Godot Z
		
	return {
		"scale_x": scale_x,
		"scale_z": scale_z,
		"raw": raw,
		"target": target_pos
	}

func _draw_selection(pos: Vector3, is_target: bool = false):
	# Ideally simple sphere or cross
	pass

func _process(delta):
	# Redraw lines
	if not debug_mesh: return
	
	var m = debug_mesh.mesh as ImmediateMesh
	m.clear_surfaces()
	m.surface_begin(Mesh.PRIMITIVE_LINES)
	
	if selected_obj_idx != -1 and selected_obj_idx < terrain_node._spawned_objects_parents.size():
		var parent = terrain_node._spawned_objects_parents[selected_obj_idx]
		var start = parent.position
		
		# Draw line to Target
		m.surface_set_color(Color.YELLOW)
		m.surface_add_vertex(start)
		m.surface_add_vertex(target_pos)
		
		# Draw Target Cross
		var s = 0.5
		m.surface_set_color(Color.GREEN)
		m.surface_add_vertex(target_pos + Vector3(-s, 0, 0))
		m.surface_add_vertex(target_pos + Vector3(s, 0, 0))
		m.surface_add_vertex(target_pos + Vector3(0, 0, -s))
		m.surface_add_vertex(target_pos + Vector3(0, 0, s))
		m.surface_add_vertex(target_pos + Vector3(0, -s, 0))
		m.surface_add_vertex(target_pos + Vector3(0, s, 0))
		
	m.surface_end()
