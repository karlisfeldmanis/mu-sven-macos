@tool
class_name MUMeshBuilder

## Mesh Builder (Phase 2)
##
## Converts BMD mesh data to Godot ArrayMesh with proper coordinate conversion
## Based on ZzzObject.cpp rendering logic

const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")

## Build a Godot ArrayMesh from BMD mesh data
static func build_mesh(bmd_mesh: BMDParser.BMDMesh, 
		skeleton: Skeleton3D = null, 
		bmd_path: String = "",
		parser: BMDParser = null,
		bake_pose: bool = false,
		debug: bool = false) -> ArrayMesh:
	if not bmd_mesh or bmd_mesh.vertices.is_empty():
		MULogger.error("[Mesh Builder] Invalid or empty mesh data")
		return null
	
	# Pre-calculate global bone transforms (Action 0, Frame 0) if baking pose
	var bone_transforms: Array[Transform3D] = []
	if bake_pose and parser and not parser.bones.is_empty():
		bone_transforms.resize(parser.bones.size())
		var action0 = parser.actions[0] if not parser.actions.is_empty() else null
		
		for i in range(parser.bones.size()):
			var bone = parser.bones[i]
			var pos = bone.position
			var rot = bone.rotation
			
			if action0 and action0.keys.size() > i and action0.keys[i] != null and not action0.keys[i].is_empty():
				var key0 = action0.keys[i][0]
				pos = key0.position
				rot = key0.rotation
			
			# Create MU-space transform (Z-up)
			var mu_quat = MUCoordinateUtils.bmd_angle_to_quaternion(rot)
			var local_transform = Transform3D(Basis(mu_quat), pos)
			
			if bone.parent_index != -1:
				bone_transforms[i] = bone_transforms[bone.parent_index] * local_transform
			else:
				bone_transforms[i] = local_transform
	
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# MU uses 0-2-1 winding (Phase 2 fix)
	for tri in bmd_mesh.triangles:
		for i in [0, 2, 1]:
			var v_idx = tri.vertex_indices[i]
			var n_idx = tri.normal_indices[i]
			var uv_idx = tri.uv_indices[i]
			
			var raw_pos = bmd_mesh.vertices[v_idx]
			var raw_normal = bmd_mesh.normals[n_idx]
			var node = bmd_mesh.vertex_nodes[v_idx]
			
			# Static Transform Baking (MU Space)
			if bake_pose and node < bone_transforms.size():
				raw_pos = bone_transforms[node] * raw_pos
				raw_normal = bone_transforms[node].basis * raw_normal
			
			# Convert to Godot space (Y-up)
			var pos = MUCoordinateUtils.convert_position(raw_pos)
			var normal = MUCoordinateUtils.convert_normal(raw_normal)
			
			var uv = bmd_mesh.uv_coords[uv_idx]
			
			st.set_normal(normal.normalized())
			st.set_uv(uv)
			st.set_color(Color.WHITE)
			
			# Single-bone skinning (if not baked, or for consistency)
			st.set_bones(PackedInt32Array([node, 0, 0, 0]))
			st.set_weights(PackedFloat32Array([1.0, 0.0, 0.0, 0.0]))
			
			st.add_vertex(pos)
	
	# Optimize and generate attributes
	st.generate_normals()
	st.generate_tangents()
	st.index()
	var mesh = st.commit()
	
	# Automated Texture Resolution
	if not bmd_path.is_empty() and bmd_mesh.texture_filename:
		var tex_path = MUTextureResolver.resolve_texture_path(bmd_path, bmd_mesh.texture_filename)
		if not tex_path.is_empty():
			var texture: Texture2D = null
			var ext = tex_path.get_extension().to_lower()
			var is_mu_format = ext in ["ozj", "ozt", "ozb"]
			
			# Only try Godot's load if it exists and is a standard format
			if not is_mu_format and FileAccess.file_exists(tex_path):
				texture = load(tex_path)
			
			# Fallback: MU formats or failed standard load
			if not texture:
				texture = _direct_load_mu_texture(tex_path)
				
			if texture:
				print("  [Mesh Builder] Texture loaded successfully: ", tex_path, " (", texture.get_width(), "x", texture.get_height(), ")")
				var material = MUMaterialFactory.create_material(texture, bmd_mesh.flags, bmd_mesh.texture_filename)
				mesh.surface_set_material(0, material)
				print("  [Mesh Builder] Material applied to surface 0")
			else:
				print("  [Mesh Builder] FAILED to load texture: ", tex_path)
		else:
			print("  [Mesh Builder] Could not resolve texture path for: ", bmd_mesh.texture_filename)
	elif bmd_path.is_empty():
		print("  [Mesh Builder] No BMD path provided, skipping texture resolution")
	
	if debug:
		print("[Mesh Builder] Created mesh:")
		print("  Vertices: ", bmd_mesh.vertex_count)
		print("  Triangles: ", bmd_mesh.triangle_count)
	
	return mesh

## Create a MeshInstance3D node with the built mesh
static func create_mesh_instance(bmd_mesh: BMDParser.BMDMesh, 
		skeleton: Skeleton3D = null, 
		bmd_path: String = "",
		parser: BMDParser = null,
		bake_pose: bool = false) -> MeshInstance3D:
	var mesh = build_mesh(bmd_mesh, skeleton, bmd_path, parser, bake_pose)
	if not mesh:
		return null
	
	var mesh_instance = MeshInstance3D.new()
	mesh_instance.mesh = mesh
	
	# Enable shadow casting for all objects
	mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	
	# Ensure the mesh has a skin if skeleton is provided
	if skeleton:
		var skin = Skin.new()
		skin.set_bind_count(skeleton.get_bone_count())
		for j in range(skeleton.get_bone_count()):
			skin.set_bind_bone(j, j)
			skin.set_bind_pose(j, Transform3D.IDENTITY)
		mesh_instance.skin = skin
		
		# Set skeleton path (caller must add to tree for this to be absolute)
		# For now use the name/relative path
		mesh_instance.skeleton = NodePath("..")
	
	return mesh_instance

## Fallback: Directly load and decrypt an OZJ/OZT file as an ImageTexture
static func _direct_load_mu_texture(path: String) -> ImageTexture:
	return MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(path))
