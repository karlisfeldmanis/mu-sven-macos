@tool
class_name MUMeshBuilder

## Mesh Builder (Phase 2)
##
## Converts BMD mesh data to Godot ArrayMesh with proper coordinate conversion
## Based on ZzzObject.cpp rendering logic

const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")

## Build a Godot ArrayMesh from BMD mesh data
static func build_mesh(bmd_mesh: BMDParser.BMDMesh, 
		_skeleton: Skeleton3D = null, 
		_bmd_path: String = "",
		debug: bool = false) -> ArrayMesh:
	if not bmd_mesh or bmd_mesh.vertices.is_empty():
		push_error("[Mesh Builder] Invalid or empty mesh data")
		return null
	
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
			
			# Keep vertices in BONE-LOCAL SPACE (MU Native Rendering)
			# We will use Identity Inverse Bind Matrices in the Skin resource
			# to achieve MU's "Transform * Local_Vertex" behavior in Godot.
			var pos = MUCoordinateUtils.convert_position(raw_pos)
			var normal = MUCoordinateUtils.convert_normal(raw_normal)
			
			var uv = bmd_mesh.uv_coords[uv_idx]
			
			st.set_normal(normal.normalized())
			st.set_uv(uv)
			st.set_color(Color.WHITE)
			
			# Single-bone skinning
			st.set_bones(PackedInt32Array([node, 0, 0, 0]))
			st.set_weights(PackedFloat32Array([1.0, 0.0, 0.0, 0.0]))
			
			st.add_vertex(pos)
	
	# Optimize and generate attributes
	st.generate_normals()
	st.generate_tangents()
	st.index()
	var mesh = st.commit()
	
	# Automated Texture Resolution
	if not _bmd_path.is_empty() and bmd_mesh.texture_filename:
		var tex_path = MUTextureResolver.resolve_texture_path(_bmd_path, bmd_mesh.texture_filename)
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
				var material = MUMaterialFactory.create_material(texture, bmd_mesh.flags)
				mesh.surface_set_material(0, material)
	
	if debug:
		print("[Mesh Builder] Created mesh:")
		print("  Vertices: ", bmd_mesh.vertex_count)
		print("  Triangles: ", bmd_mesh.triangle_count)
	
	return mesh

## Create a MeshInstance3D node with the built mesh
static func create_mesh_instance(bmd_mesh: BMDParser.BMDMesh, 
		skeleton: Skeleton3D = null, 
		bmd_path: String = "") -> MeshInstance3D:
	var mesh = build_mesh(bmd_mesh, skeleton, bmd_path)
	if not mesh:
		return null
	
	var mesh_instance = MeshInstance3D.new()
	mesh_instance.mesh = mesh
	
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
