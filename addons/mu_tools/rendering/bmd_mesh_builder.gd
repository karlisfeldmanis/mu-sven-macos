@tool
# class_name MUMeshBuilder

## Mesh Builder (Phase 2)
##
## Converts BMD mesh data to Godot ArrayMesh with proper coordinate conversion
## Based on ZzzObject.cpp rendering logic

const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")
const MULogger = preload("res://addons/mu_tools/util/mu_logger.gd")
const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")
# const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd") # Dynamic load
const MUTextureResolver = preload("res://addons/mu_tools/rendering/texture_resolver.gd")
const MUMaterialFactory = preload("res://addons/mu_tools/rendering/material_factory.gd")
static var _mesh_cache: Dictionary = {} # String (path_idx_bake) -> ArrayMesh
static var _registry = null

static func get_registry():
	if not _registry:
		_registry = load("res://addons/mu_tools/core/registry.gd")
	return _registry

## Build a Godot ArrayMesh from BMD mesh data
static func build_mesh(bmd_mesh: Variant, 
		_skeleton: Skeleton3D = null, 
		bmd_path: String = "",
		_parser: Variant = null,
		bake_pose: bool = false,
		_debug: bool = false,
		no_texture: bool = false,
		surface_index: int = 0,
		animated_materials: Array = []) -> ArrayMesh:
	if not bmd_mesh or bmd_mesh.vertices.is_empty():
		return null
	
	var cache_key = ""
	if not bmd_path.is_empty():
		cache_key = bmd_path + "_" + str(bmd_mesh.get_instance_id()) + "_" + str(bake_pose) + "_" + str(no_texture)
		if _mesh_cache.has(cache_key):
			return _mesh_cache[cache_key]
	
	# Pre-calculate global bone transforms (Bind Pose)
	# SVEN Parity: World objects always use Action 0 as the reference/bind pose
	# (ZzzObject.cpp:4473 — CreateObject sets o->CurrentAction = 0)
	var bone_transforms: Array[Transform3D] = []
	if bake_pose and _parser and not _parser.bones.is_empty():
		var forced_action = get_registry().get_bind_pose_action(bmd_path)
		var action_to_use = null
		bone_transforms.resize(_parser.bones.size())

		if forced_action != -1 and _parser.actions.size() > forced_action:
			action_to_use = _parser.actions[forced_action]
		elif _parser.actions.size() > 0:
			action_to_use = _parser.actions[0]
		
		# Resolve hierarchy (Topological order)
		var bones_done = []
		bones_done.resize(_parser.bones.size())
		bones_done.fill(false)
		
		# Multi-pass resolution to handle any bone ordering
		for pass_idx in range(16):
			var all_done = true
			for i in range(_parser.bones.size()):
				if bones_done[i]: continue
				var bone = _parser.bones[i]
				var p_idx = bone.parent_index
				
				if p_idx == -1 or bones_done[p_idx]:
					var pos = bone.position
					var rot = bone.rotation
					
					if (action_to_use and action_to_use.keys.size() > i and 
							action_to_use.keys[i] != null and not action_to_use.keys[i].is_empty()):
						var key0 = action_to_use.keys[i][0]
						pos = key0.position
						rot = key0.rotation
					
					var local_t = MUTransformPipeline.build_local_transform(pos, rot)
					if p_idx != -1:
						bone_transforms[i] = bone_transforms[p_idx] * local_t
					else:
						# Root bone: include position as-is for world parity
						bone_transforms[i] = local_t
					bones_done[i] = true
			if all_done: break
	
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# WARNING: [0,2,1] reverses MU CW -> Godot CCW winding.
	# This is the ONLY place winding is fixed. Do NOT add scale mirrors elsewhere.
	# See docs/COORDINATE_SYSTEM.md
	for tri in bmd_mesh.triangles:
		for i in [0, 2, 1]:
			var v_idx = tri.vertex_indices[i]
			var n_idx = tri.normal_indices[i]
			var uv_idx = tri.uv_indices[i]

			var raw_pos = bmd_mesh.vertices[v_idx]
			var raw_normal = bmd_mesh.normals[n_idx]
			var node = bmd_mesh.vertex_nodes[v_idx]
			var normal_node = (bmd_mesh.normal_nodes[n_idx] if n_idx < bmd_mesh.normal_nodes.size() 
					else node)

			# CENTRAL TRANSFORMATION (Godot-space Frame)
			var pos = MUTransformPipeline.local_mu_to_godot(raw_pos)
			# Cyclic permutation (det=+1): normals map directly, no negation needed
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
						bmd_path, surface_index, animated_materials)
				mesh.surface_set_material(0, material)
			else:
				# Texture loaded but returned null (corrupt or unsupported)
				var mat = StandardMaterial3D.new()
				mat.albedo_color = Color.MAGENTA # Diagnostic Purple
				mesh.surface_set_material(0, mat)
		else:
			# Texture resolution failed (File not found)
			print("[Mesh Builder] Texture Resolution FAILED for: ", bmd_mesh.texture_filename, " (Path: ", bmd_path, ")")
			var mat = StandardMaterial3D.new()
			mat.albedo_color = Color.MAGENTA # Diagnostic Purple
			mesh.surface_set_material(0, mat)
	elif no_texture:
		# Apply flat grey material for geometry inspection
		var mat = StandardMaterial3D.new()
		mat.albedo_color = Color.GRAY
		mat.cull_mode = BaseMaterial3D.CULL_DISABLED
		mesh.surface_set_material(0, mat)
	
	# if debug:
	# 	print("[Mesh Builder] Created mesh:")
	# 	print("  Vertices: ", bmd_mesh.vertex_count)
	# 	print("  Triangles: ", bmd_mesh.triangle_count)
	
	if not cache_key.is_empty():
		_mesh_cache[cache_key] = mesh
	return mesh

## Create a MeshInstance3D node with the built mesh
static func create_mesh_instance(bmd_mesh: Variant, 
		skeleton: Skeleton3D = null, 
		path: String = "",
		parser: Variant = null,
		bake_pose: bool = false,
		no_skin: bool = false,
		no_texture: bool = false,
		surface_index: int = 0,
		animated_materials: Array = []) -> MeshInstance3D:
	var mesh = build_mesh(
			bmd_mesh, skeleton, path, parser, bake_pose, false, no_texture, surface_index
	)
	if not mesh:
		return null
	
	var mesh_instance = MeshInstance3D.new()
	mesh_instance.mesh = mesh
	
	# Enable shadow casting for all objects
	mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	
	# Ensure the mesh has a skin if skeleton is provided
	# 🔴 FIX: Avoid double-skinning if vertices are already baked on CPU
	if skeleton and not no_skin and not bake_pose:
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

## Create a MeshInstance3D node with the built mesh (Extended for specific bind pose)
static func create_mesh_instance_v2(bmd_mesh: Variant, 
		skeleton: Skeleton3D = null, 
		path: String = "",
		parser: Variant = null,
		bake_pose: bool = false,
		no_skin: bool = false,
		no_texture: bool = false,
		surface_index: int = 0,
		forced_action_idx: int = -1,
		animated_materials: Array = []) -> MeshInstance3D:
	var mesh = build_mesh_v2(
			bmd_mesh, skeleton, path, parser, bake_pose, false, no_texture, surface_index, forced_action_idx, animated_materials
	)
	if not mesh:
		return null
	
	var mesh_instance = MeshInstance3D.new()
	mesh_instance.mesh = mesh
	mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	
	if skeleton and not no_skin and not bake_pose:
		var skin = Skin.new()
		skin.set_bind_count(skeleton.get_bone_count())
		for j in range(skeleton.get_bone_count()):
			skin.set_bind_bone(j, j)
			var inv_rest = skeleton.get_bone_global_rest(j).affine_inverse()
			skin.set_bind_pose(j, inv_rest)
		mesh_instance.skin = skin
		mesh_instance.skeleton = NodePath("..")
	
	return mesh_instance

## Internal: build_mesh with forced action index support
static func build_mesh_v2(bmd_mesh: Variant, 
		_skeleton: Skeleton3D = null, 
		bmd_path: String = "",
		_parser: Variant = null,
		bake_pose: bool = false,
		_debug: bool = false,
		no_texture: bool = false,
		surface_index: int = 0,
		forced_action_idx: int = -1,
		animated_materials: Array = []) -> ArrayMesh:
	if not bmd_mesh or bmd_mesh.vertices.is_empty():
		return null
	
	# Cache includes action index
	var cache_key = ""
	if not bmd_path.is_empty():
		cache_key = bmd_path + "_" + str(bmd_mesh.get_instance_id()) + "_" + str(bake_pose) + "_a" + str(forced_action_idx) + "_" + str(no_texture)
		if _mesh_cache.has(cache_key):
			return _mesh_cache[cache_key]
	
	var bone_transforms: Array[Transform3D] = []
	if bake_pose and _parser and not _parser.bones.is_empty():
		var action_to_use = null
		bone_transforms.resize(_parser.bones.size())

		if forced_action_idx != -1 and _parser.actions.size() > forced_action_idx:
			action_to_use = _parser.actions[forced_action_idx]
		else:
			var default_action = get_registry().get_bind_pose_action(bmd_path)
			if default_action != -1 and _parser.actions.size() > default_action:
				action_to_use = _parser.actions[default_action]
			elif _parser.actions.size() > 0:
				action_to_use = _parser.actions[0]
		
		# Resolve hierarchy
		var bones_done = []
		bones_done.resize(_parser.bones.size())
		bones_done.fill(false)
		
		for pass_idx in range(16):
			var all_done = true
			for i in range(_parser.bones.size()):
				if bones_done[i]: continue
				var bone = _parser.bones[i]
				var p_idx = bone.parent_index
				
				if p_idx == -1 or bones_done[p_idx]:
					var pos = bone.position
					var rot = bone.rotation
					
					if (action_to_use and action_to_use.keys.size() > i and 
							action_to_use.keys[i] != null and not action_to_use.keys[i].is_empty()):
						var key0 = action_to_use.keys[i][0]
						pos = key0.position
						rot = key0.rotation
					
					var local_t = MUTransformPipeline.build_local_transform(pos, rot)
					if p_idx != -1:
						bone_transforms[i] = bone_transforms[p_idx] * local_t
					else:
						bone_transforms[i] = local_t
					bones_done[i] = true
			if all_done: break
	
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	for tri in bmd_mesh.triangles:
		for i in [0, 2, 1]:
			var v_idx = tri.vertex_indices[i]
			var n_idx = tri.normal_indices[i]
			var uv_idx = tri.uv_indices[i]
			var raw_pos = bmd_mesh.vertices[v_idx]
			var raw_normal = bmd_mesh.normals[n_idx]
			var node = bmd_mesh.vertex_nodes[v_idx]
			var normal_node = (bmd_mesh.normal_nodes[n_idx] if n_idx < bmd_mesh.normal_nodes.size() else node)

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
			st.set_bones(PackedInt32Array([node, 0, 0, 0]))
			st.set_weights(PackedFloat32Array([1.0, 0.0, 0.0, 0.0]))
			st.add_vertex(pos)
	
	st.generate_tangents()
	st.index()
	var mesh = st.commit()
	
	if not no_texture and not bmd_path.is_empty() and bmd_mesh.texture_filename:
		var tex_path = MUTextureResolver.resolve_texture_path(bmd_path, bmd_mesh.texture_filename)
		if not tex_path.is_empty():
			var texture: Texture2D = null
			var ext = tex_path.get_extension().to_lower()
			if ext not in ["ozj", "ozt", "ozb"] and ResourceLoader.exists(tex_path):
				texture = load(tex_path)
			if not texture:
				texture = _direct_load_mu_texture(tex_path)
			if texture:
				var material = MUMaterialFactory.create_material(
						texture, 0, bmd_mesh.texture_filename,
						bmd_path, surface_index, animated_materials)
				mesh.surface_set_material(0, material)
			else:
				var mat = StandardMaterial3D.new()
				mat.albedo_color = Color(1, 0, 1, 0.5) # Failed to load
				mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				mesh.surface_set_material(0, mat)
		else:
			var mat = StandardMaterial3D.new()
			mat.albedo_color = Color(1, 0, 1, 0.5) # Failed to resolve
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			mesh.surface_set_material(0, mat)
	elif no_texture:
		var mat = StandardMaterial3D.new()
		mat.albedo_color = Color.GRAY
		mat.cull_mode = BaseMaterial3D.CULL_DISABLED
		mesh.surface_set_material(0, mat)
	
	if not cache_key.is_empty():
		_mesh_cache[cache_key] = mesh
	return mesh

## Fallback: Directly load and decrypt an OZJ/OZT file as an ImageTexture
static func _direct_load_mu_texture(path: String) -> ImageTexture:
	print("  [Mesh Builder] Direct loading MU texture: ", path)
	return MUTextureLoader.load_mu_texture(path)
