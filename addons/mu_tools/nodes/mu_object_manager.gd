extends Node3D

class_name MUObjectManager


const ParserClass = preload("res://addons/mu_tools/core/mu_terrain_parser.gd")
const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/nodes/mesh_builder.gd")
const MUSkeletonBuilder = preload("res://addons/mu_tools/ui/skeleton_builder.gd")
const MUObjLoader = preload("res://addons/mu_tools/core/mu_obj_loader.gd")

## Spawns all objects defined in the map's object placement file
static func load_objects(parent: Node3D, objects: Array, show_debug: bool = false, heights: PackedFloat32Array = PackedFloat32Array(), show_hidden: bool = false, world_id: int = 1, filter_rect: Rect2 = Rect2()) -> void:
	print("[Object Manager] Spawning objects (World %d)..." % world_id)
	
	var count = 0
	var missing = {}
	var broken = {}
	
	# Use explicit preload to ensure static function access
	const Registry = preload("res://addons/mu_tools/core/mu_model_registry.gd")
	
	for obj_data in objects:
		var pos = obj_data.position
		
		# Spatial Filter check
		if filter_rect != Rect2() and not filter_rect.has_point(Vector2(pos.x, pos.z)):
			continue
			
		var path = Registry.get_object_path(obj_data.type, world_id)
		if path == "":
			if not missing.has(obj_data.type):
				missing[obj_data.type] = true
				print("  [Object Manager] WARNING: Unmapped Object Type: ", obj_data.type)
			continue
			
		# Spawning ALL objects for full city center comparison
		# var is_wall = Registry.is_wall(obj_data.type, world_id)
		# if not is_wall:
		# 	continue
			
		var instance: Node3D = null
		if path.to_lower().ends_with("bmd"):
			instance = _load_bmd(path, false)
		else:
			push_error("[Object Manager] Unsupported asset format: " + path)
			
		instance.name = "Obj_%d_%d" % [obj_data.type, count]
		parent.add_child(instance)
		
		# Apply Transforms
		# Position X/Z are correct from MUTerrainParser.
		# Height (Y) needs terrain snapping — parsed MU Z uses raw cm/100 scale,
		# but the terrain mesh uses (raw * 1.5 - 500) / 100 scale.
		var godot_pos = obj_data.position
		
		# 🟢 Absolute Height Resolution
		# Only snap to terrain if the object is clearly meant to be ground-based (height < threshold)
		# MU city center floor is ~165. Roofs are >400.
		if not heights.is_empty():
			var tile_x = int(clamp(floor(godot_pos.z), 0, 255))
			var tile_y = int(clamp(floor(godot_pos.x), 0, 255))
			var h_idx = tile_y * 256 + tile_x
			if h_idx < heights.size():
				var ground_h = heights[h_idx]
				# If object is significantly above ground (> 1.0m), keep its original height.
				# Otherwise, snap to terrain for perfect alignment.
				if godot_pos.y < ground_h + 1.0:
					godot_pos.y = ground_h
				
		instance.position = godot_pos
		
		# Log placement removed
		
		# 🟢 APPLY ROTATION OVERRIDES (Centralized Registry)
		var base_rot = obj_data.rotation
		# [/] Refine mirroring: Debug wall corner with logging.
		var override_deg = Registry.get_rotation_override(path)
		if show_debug:
			print("  [Object Manager Debug] Model Path: %s, Override: %s" % [path, override_deg])
		if override_deg != Vector3.ZERO:
			var ov_rad = Vector3(deg_to_rad(override_deg.x), deg_to_rad(override_deg.y), deg_to_rad(override_deg.z))
			var ov_q = MUTransformPipeline.mu_rotation_to_quaternion(ov_rad)
			# Combine: Raw * Override (Local offset)
			instance.quaternion = base_rot * ov_q
		else:
			instance.quaternion = base_rot

		# Restore mirroring via MUCoordinateUtils to fix chirality/swastika artifacts.
		instance.scale = MUCoordinateUtils.convert_scale(obj_data.scale)
		
		# Diagnostic for specific area
		if pos.x > 12000 and pos.x < 13000 and pos.z > 12000 and pos.z < 13000:
			print("  [DEBUG AREA] Obj %d type %d at %s: MapPath=%s Override=%s" % [count, obj_data.type, pos, path.get_file(), override_deg])
		
		# Visibility Logic (Sven Parity)
		if obj_data.hidden_mesh == -2:
			instance.visible = show_hidden 
		else:
			instance.visible = true
			
		count += 1
			
	print("✓ Robust Spawn Complete. %d objects placed." % count)

static func _load_obj(path: String) -> Node3D:
	return MUObjLoader.build_mesh_instance(path)

static func _load_bmd(path: String, debug: bool = false) -> Node3D:
	var parser = BMDParser.new()
	if not parser.parse_file(path, debug):
		return null
		
	var root = Node3D.new()
	var skeleton: Skeleton3D = null
	
	if not parser.bones.is_empty():
		skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
		root.add_child(skeleton)
		
	for bmd_mesh in parser.meshes:
		# 🔴 Enable bake_pose (5th arg) for static world objects to avoid skinning artifacts
		var mi = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, path, parser, true, false, false, 0)
		if mi:
			if skeleton:
				root.add_child(mi) # Parent to root instead of skeleton for bake_pose parity
			else:
				root.add_child(mi)
				
	return root
