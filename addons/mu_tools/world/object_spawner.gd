extends Node3D

# class_name MUObjectManager


const ParserClass = preload("res://addons/mu_tools/parsers/terrain_parser.gd")
const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/rendering/bmd_mesh_builder.gd")
const MUSkeletonBuilder = preload("res://addons/mu_tools/ui/skeleton_builder.gd")
const MUObjLoader = preload("res://addons/mu_tools/parsers/obj_loader.gd")
const MUObjectEffectManager = preload("res://addons/mu_tools/nodes/mu_object_effect_manager.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")

## Spawns all objects defined in the map's object placement file
static func load_objects(parent: Node3D, objects: Array, show_debug: bool = false, heights: PackedFloat32Array = PackedFloat32Array(), show_hidden: bool = false, world_id: int = 1, filter_rect: Rect2 = Rect2()) -> void:
	print("[Object Manager] Spawning objects (World %d)..." % world_id)
	
	var count = 0
	var missing = {}
	var broken = {}
	
	# Use explicit preload to ensure static function access
	const Registry = preload("res://addons/mu_tools/core/registry.gd")
	
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
			var ground_h = MUCoordinateUtils.sample_height_bilinear(godot_pos, heights)
			
			# Objects within 1.0m of ground are snapped.
			# Grass (types 20-27) is snapped with higher tolerance (5m) to ensure contact on slopes.
			var snap_limit = 1.0
			if obj_data.type >= 20 and obj_data.type <= 27:
				snap_limit = 5.0
				
			if godot_pos.y < ground_h + snap_limit:
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

		instance.scale = MUCoordinateUtils.convert_scale(obj_data.scale)
		
		# Diagnostic for specific area
		if pos.x > 12000 and pos.x < 13000 and pos.z > 12000 and pos.z < 13000:
			print("  [DEBUG AREA] Obj %d type %d at %s: MapPath=%s Override=%s" % [count, obj_data.type, pos, path.get_file(), override_deg])
		
		# Visibility Logic (Sven Parity)
		if obj_data.hidden_mesh == -2:
			instance.visible = show_hidden 
		else:
			instance.visible = true
			
		# 🟢 APPLY VISUAL EFFECTS (Wind, Light, UV Scrollers, Fire, etc.)
		MUObjectEffectManager.apply_effects(instance, obj_data.type, world_id)
			
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
