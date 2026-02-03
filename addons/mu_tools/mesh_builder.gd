@tool
class_name MUMeshBuilder

## Mesh Builder (Phase 2)
##
## Converts BMD mesh data to Godot ArrayMesh with proper coordinate conversion
## Based on ZzzObject.cpp rendering logic

## Build a Godot ArrayMesh from BMD mesh data
static func build_mesh(bmd_mesh: BMDParser.BMDMesh, 
		_skeleton: Skeleton3D = null, 
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
	
	if debug:
		print("[Mesh Builder] Created mesh:")
		print("  Vertices: ", bmd_mesh.vertex_count)
		print("  Triangles: ", bmd_mesh.triangle_count)
	
	return mesh

## Create a MeshInstance3D node with the built mesh
static func create_mesh_instance(bmd_mesh: BMDParser.BMDMesh, 
		skeleton: Skeleton3D = null, 
		texture_path: String = "") -> MeshInstance3D:
	var mesh = build_mesh(bmd_mesh, skeleton)
	if not mesh:
		return null
	
	var mesh_instance = MeshInstance3D.new()
	mesh_instance.mesh = mesh
	
	# Apply MU specialized material
	if not texture_path.is_empty() and FileAccess.file_exists(texture_path):
		var texture = load(texture_path)
		if texture:
			var material = MUMaterialFactory.create_material(texture, bmd_mesh.flags)
			mesh_instance.set_surface_override_material(0, material)
	
	return mesh_instance
