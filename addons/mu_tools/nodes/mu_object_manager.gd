extends Node3D

class_name MUObjectManager


const ParserClass = preload("res://addons/mu_tools/core/mu_terrain_parser.gd")
const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/nodes/mesh_builder.gd")
const MUSkeletonBuilder = preload("res://addons/mu_tools/ui/skeleton_builder.gd")
# const DebugAxes = preload("res://addons/mu_tools/debug_axes.gd") # Missing

## Spawns all objects defined in the map's object placement file
static func load_objects(parent: Node3D, objects: Array, show_debug: bool = false, heights: PackedFloat32Array = PackedFloat32Array(), show_hidden: bool = false, world_id: int = 1) -> void:
	print("[Object Manager] Spawning %d world objects with Robust Logic (World %d)..." % [objects.size(), world_id])
	
	var count = 0
	var missing = {}
	var broken = {}
	var MUModelRegistry = load("res://addons/mu_tools/core/mu_model_registry.gd")
	
	for obj_data in objects:
		var path = MUModelRegistry.get_object_path(obj_data.type, world_id)
		if path == "":
			if not missing.has(obj_data.type):
				missing[obj_data.type] = true
				print("  [Object Manager] WARNING: Unmapped Object Type: ", obj_data.type)
			continue
			
		var instance: Node3D = null
		var ext = path.get_extension().to_lower()
		
		if ext == "obj":
			instance = _load_obj(path)
		elif ext == "bmd":
			instance = _load_bmd(path)
		
		if not instance:
			if not broken.has(path):
				broken[path] = true
				print("  [Object Manager] ERROR: Failed to load asset: ", path)
			continue
			
		instance.name = "Obj_%d_%d" % [obj_data.type, count]
		parent.add_child(instance)
		
		# Apply Transforms
		var pos = obj_data.position
		
		# Terrain Snapping Logic
		if not heights.is_empty():
			# Map Godot World Position back to MU Tile Indices
			# GodotX = mu_x | GodotZ = 255 - mu_y
			var mu_x = int(clamp(floor(pos.x), 0, 255))
			var mu_y = int(clamp(floor(255.0 - pos.z), 0, 255))
			
			var h_idx = mu_y * 256 + mu_x
			if h_idx < heights.size():
				pos.y = heights[h_idx]
		
		instance.position = pos
		instance.quaternion = obj_data.rotation
		instance.scale = Vector3(obj_data.scale, obj_data.scale, obj_data.scale)
		
		# Visibility Logic (Sven Parity)
		if obj_data.hidden_mesh == -2:
			instance.visible = show_hidden # Forced by user or standard SVEN logic
		else:
			instance.visible = true
		
		# Debug Visuals
		if show_debug:
			pass
			# var axes = DebugAxes.new()
			# axes.axis_length = 5.0 # Large enough to see
			# instance.add_child(axes)
			
		count += 1
			
	print("✓ Robust Spawn Complete. %d objects placed." % count)

static func _load_obj(path: String) -> Node3D:
	var res = load(path)
	if res and res is Mesh:
		var instance = MeshInstance3D.new()
		instance.mesh = res
		
		# LATE DISCOVERY FALLBACK (Same as Model Viewer)
		# Path is extracted_data/object_models/SomeModel.obj
		var mesh_dir = path.get_base_dir()
		
		for i in range(res.get_surface_count()):
			var mat = res.surface_get_material(i)
			if mat is BaseMaterial3D:
				# Double-sided is essential for MU objects
				mat.cull_mode = BaseMaterial3D.CULL_DISABLED
				
				# If we have no texture, try to find it
				if not mat.albedo_texture:
					var s_name = res.surface_get_name(i)
					if s_name.is_empty():
						s_name = mat.resource_name
						
					if not s_name.is_empty():
						var possible_names = [
							s_name + ".png",
							s_name + ".PNG",
							s_name.to_lower() + ".png"
						]
						for pn in possible_names:
							var tex_path = mesh_dir.path_join(pn)
							if FileAccess.file_exists(tex_path):
								var global_path = ProjectSettings.globalize_path(tex_path)
								var img = Image.load_from_file(global_path)
								if img:
									var tex = ImageTexture.create_from_image(img)
									mat.albedo_texture = tex
									# print("  [Object Manager] ✓ Patching: ", pn)
									break
		
		return instance
	return null

static func _load_bmd(path: String) -> Node3D:
	var parser = BMDParser.new()
	if not parser.parse_file(path):
		return null
		
	var root = Node3D.new()
	var skeleton: Skeleton3D = null
	
	if not parser.bones.is_empty():
		skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
		root.add_child(skeleton)
		
	for bmd_mesh in parser.meshes:
		var mi = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, path, parser)
		if mi:
			if skeleton:
				skeleton.add_child(mi)
			else:
				root.add_child(mi)
				
	return root
