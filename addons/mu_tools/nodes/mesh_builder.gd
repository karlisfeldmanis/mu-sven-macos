@tool
class_name MUMeshBuilder

## Mesh Builder (Phase 2)
##
## Converts BMD mesh data to Godot ArrayMesh with proper coordinate conversion
## Based on ZzzObject.cpp rendering logic

const MUTextureLoader = preload("res://addons/mu_tools/core/mu_texture_loader.gd")
const MULogger = preload("res://addons/mu_tools/util/mu_logger.gd")
const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")
# const MUModelRegistry = preload("res://addons/mu_tools/core/mu_model_registry.gd") # Dynamic load
const MUTextureResolver = preload("res://addons/mu_tools/util/mu_texture_resolver.gd")
const MUMaterialFactory = preload("res://addons/mu_tools/nodes/mu_material_factory.gd")
static var _mesh_cache: Dictionary = {} # String (path_idx_bake) -> ArrayMesh
static var _Registry = null

static func get_registry():
	if not _Registry:
		_Registry = load("res://addons/mu_tools/core/mu_model_registry.gd")
	return _Registry

## Build a Godot ArrayMesh from BMD mesh data
static func build_mesh(bmd_mesh: BMDParser.BMDMesh, 
		_skeleton: Skeleton3D = null, 
		bmd_path: String = "",
		_parser: BMDParser = null,
		bake_pose: bool = false,
		_debug: bool = false,
		no_texture: bool = false,
		surface_index: int = 0) -> ArrayMesh:
	if not bmd_mesh or bmd_mesh.vertices.is_empty():
		return null
	
	var cache_key = ""
	if not bmd_path.is_empty():
		cache_key = bmd_path + "_" + str(bmd_mesh.get_instance_id()) + "_" + str(bake_pose)
		if _mesh_cache.has(cache_key):
			return _mesh_cache[cache_key]
	
	# Pre-calculate global bone transforms (Bind Pose)
	var bone_transforms: Array[Transform3D] = []
	if bake_pose and _parser and not _parser.bones.is_empty():
		# Resolve Bind Pose Selection
		var forced_action = get_registry().get_bind_pose_action(bmd_path)
		var action_to_use = null
		bone_transforms.resize(_parser.bones.size())
		
		if forced_action != -1 and _parser.actions.size() > forced_action:
			action_to_use = _parser.actions[forced_action]
		else:
			# Fallback to Origin-Proximity Heuristic
			if _parser.actions.size() > 0:
				action_to_use = _parser.actions[0]
				if _parser.actions.size() > 1:
					var keys0 = _parser.actions[0].keys
					var keys1 = _parser.actions[1].keys
					if keys0.size() > 0 and keys1.size() > 0 and not keys0[0].is_empty() and not keys1[0].is_empty():
						var p0 = keys0[0][0].position
						var p1 = keys1[0][0].position
						if p1.length() < p0.length() - 10.0 or (p0.length() > 50.0 and p1.length() < 10.0):
							action_to_use = _parser.actions[1]
		
		for i in range(_parser.bones.size()):
			var bone = _parser.bones[i]
			var pos = bone.position
			var rot = bone.rotation
			
			if action_to_use and action_to_use.keys.size() > i and action_to_use.keys[i] != null and not action_to_use.keys[i].is_empty():
				var key0 = action_to_use.keys[i][0]
				pos = key0.position
				rot = key0.rotation
			
			# Build Authoritative Godot-space transform
			var local_transform = MUTransformPipeline.build_local_transform(pos, rot)
			
			if bone.parent_index != -1:
				bone_transforms[i] = bone_transforms[bone.parent_index] * local_transform
			else:
				bone_transforms[i] = local_transform
	
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# MU uses CW winding, Godot uses CCW. Swap 1 and 2 (0-2-1).
	for tri in bmd_mesh.triangles:
		for i in [0, 2, 1]:
			var v_idx = tri.vertex_indices[i]
			var n_idx = tri.normal_indices[i]
			var uv_idx = tri.uv_indices[i]
			
			var raw_pos = bmd_mesh.vertices[v_idx]
			var raw_normal = bmd_mesh.normals[n_idx]
			var node = bmd_mesh.vertex_nodes[v_idx]
			var normal_node = bmd_mesh.normal_nodes[n_idx] if n_idx < bmd_mesh.normal_nodes.size() else node
			
			# CENTRAL TRANSFORMATION (Godot-space Frame)
			var pos = MUTransformPipeline.local_mu_to_godot(raw_pos)
			var normal = MUTransformPipeline.mu_normal_to_godot(raw_normal)
			
			if bake_pose and node < bone_transforms.size():
				pos = bone_transforms[node] * pos
				if normal_node < bone_transforms.size():
					normal = (bone_transforms[normal_node].basis * normal).normalized()
			
			var uv = bmd_mesh.uv_coords[uv_idx]
			
			st.set_normal(normal)
			st.set_uv(uv)
			st.set_color(Color.WHITE)
			
			# Single-bone skinning
			st.set_bones(PackedInt32Array([node, 0, 0, 0]))
			st.set_weights(PackedFloat32Array([1.0, 0.0, 0.0, 0.0]))
			
			st.add_vertex(pos)
	
	# Optimize
	# NOTE: We preserve original BMD normals, do not call generate_normals()
	
	st.generate_tangents()
	st.index()
	var mesh = st.commit()
	
	# Automated Texture Resolution
	if not no_texture and not bmd_path.is_empty() and bmd_mesh.texture_filename:
		var tex_path = MUTextureResolver.resolve_texture_path(bmd_path, bmd_mesh.texture_filename)
		if not tex_path.is_empty():
			var texture: Texture2D = null
			var ext = tex_path.get_extension().to_lower()
			var is_mu_format = ext in ["ozj", "ozt", "ozb"]
			
			# Only try Godot's load if it exists and is a standard format
			if not is_mu_format and ResourceLoader.exists(tex_path):
				texture = load(tex_path)
			
			# Fallback: MU formats or failed standard load
			if not texture:
				texture = _direct_load_mu_texture(tex_path)
				
			if texture:
				var material = MUMaterialFactory.create_material(
						texture, 0, bmd_mesh.texture_filename,
						bmd_path, surface_index)
				mesh.surface_set_material(0, material)
			else:
				# MULogger.error("[Mesh Builder] FAILED to load texture")
				pass
	# elif bmd_path.is_empty():
	# 	print("  [Mesh Builder] No BMD path provided, skipping texture resolution")
	
	# if debug:
	# 	print("[Mesh Builder] Created mesh:")
	# 	print("  Vertices: ", bmd_mesh.vertex_count)
	# 	print("  Triangles: ", bmd_mesh.triangle_count)
	
	if not cache_key.is_empty():
		_mesh_cache[cache_key] = mesh
	return mesh

## Create a MeshInstance3D node with the built mesh
static func create_mesh_instance(bmd_mesh: BMDParser.BMDMesh, 
		skeleton: Skeleton3D = null, 
		path: String = "",
		parser: BMDParser = null,
		bake_pose: bool = false,
		no_skin: bool = false,
		no_texture: bool = false,
		surface_index: int = 0) -> MeshInstance3D:
	var mesh = build_mesh(bmd_mesh, skeleton, path, parser, bake_pose, false, no_texture, surface_index)
	if not mesh:
		return null
	
	var mesh_instance = MeshInstance3D.new()
	mesh_instance.mesh = mesh
	
	# Enable shadow casting for all objects
	mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	
	# Ensure the mesh has a skin if skeleton is provided
	if skeleton and not no_skin:
		var skin = Skin.new()
		skin.set_bind_count(skeleton.get_bone_count())
		for j in range(skeleton.get_bone_count()):
			skin.set_bind_bone(j, j)
			# MU vertices are baked into Model-Space (Global Rest),
			# so BindPose = GlobalRest.affine_inverse()
			var inv_rest = skeleton.get_bone_global_rest(j).affine_inverse()
			skin.set_bind_pose(j, inv_rest)
		mesh_instance.skin = skin
		
		# Set skeleton path (caller must add to tree for this to be absolute)
		# For now use the name/relative path
		mesh_instance.skeleton = NodePath("..")
	
	return mesh_instance

## Fallback: Directly load and decrypt an OZJ/OZT file as an ImageTexture
static func _direct_load_mu_texture(path: String) -> ImageTexture:
	return MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(path))
