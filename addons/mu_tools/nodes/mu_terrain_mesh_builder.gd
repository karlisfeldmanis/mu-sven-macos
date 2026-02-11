extends Node

class_name MUTerrainMeshBuilder

const TERRAIN_SIZE = 256

func build_terrain_array_mesh(heights: PackedFloat32Array, lightmap: Image) -> ArrayMesh:
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# Pass 1: Add all vertices (SVEN is 256x256 atoms -> 256 cells -> 257 vertices)
	for y in range(TERRAIN_SIZE + 1):
		for x in range(TERRAIN_SIZE + 1):
			var data = _get_v_data(x, y, heights, lightmap)
			_add_st_vertex(st, data)
	
	# Pass 2: Add Indices (256x256 cells)
	var v_count = TERRAIN_SIZE + 1
	for y in range(TERRAIN_SIZE):
		for x in range(TERRAIN_SIZE):
			var i00 = y * v_count + x
			var i10 = y * v_count + (x + 1)
			var i01 = (y + 1) * v_count + x
			var i11 = (y + 1) * v_count + (x + 1)
			
			# Godot CCW Standard: (0,0) -> (1,0) -> (1,1)
			st.add_index(i00)
			st.add_index(i10)
			st.add_index(i11)
			st.add_index(i00)
			st.add_index(i11)
			st.add_index(i01)
				
	st.generate_normals()
	st.generate_tangents()
	var mesh = st.commit()
	
	# Set AABB for correct frustum culling (256 spans)
	var aabb = AABB(Vector3(0, -50, 0), Vector3(256, 100, 256))
	mesh.custom_aabb = aabb
	
	return mesh

func _get_v_data(x: int, y: int, heights: PackedFloat32Array, lightmap: Image) -> Dictionary:
	var mu_x = clamp(x, 0, 255)
	var mu_y = clamp(y, 0, 255)
	# Baseline: Standard Mapping
	# h_idx = y * 256 + x
	var h_idx = mu_y * 256 + mu_x
	var h = float(heights[h_idx])
	
	# 🟢 Proven Mapping (Transpose)
	# Godot X = MU Y (Row)
	# Godot Z = MU X (Col)
	var pos_x = float(y)
	var pos_z = float(x)
	var pos = Vector3(pos_x, h, pos_z)
	
	# UVs: We want the shader to see MU coordinates.
	# If Godot X is mirrored, we pass the raw grid indices (x, y) as UVs.
	# The shader will use world_pos.x to derive MU X.
	var uv = Vector2(float(x) / 256.0, float(y) / 256.0)
	var uv2 = uv
	var color = Color.WHITE
	if lightmap:
		color = lightmap.get_pixel(mu_x, mu_y) 
		
	return {"pos": pos, "uv": uv, "uv2": uv2, "color": color}

func _add_st_vertex(st: SurfaceTool, data: Dictionary):
	st.set_color(data.color)
	st.set_uv(data.uv)
	st.set_uv2(data.uv2)
	st.add_vertex(data.pos)
