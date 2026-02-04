extends Node

class_name MUTerrainMeshBuilder

const TERRAIN_SIZE = 256

func build_terrain_mesh(heights: PackedFloat32Array, lightmap: Image) -> Mesh:
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# Create a 256x256 grid
	# Each tile is 1x1 Godot meter
	# Heights are already in Godot scale from parser
	
	for y in range(TERRAIN_SIZE):
		for x in range(TERRAIN_SIZE):
			# Add two triangles per grid cell
			# Winding order swapped to maintain Up normal after 90deg rotation
			# Triangle 1
			_add_vertex(st, x, y, heights, lightmap)
			_add_vertex(st, x + 1, y, heights, lightmap)
			_add_vertex(st, x + 1, y + 1, heights, lightmap)
			
			# Triangle 2
			_add_vertex(st, x, y, heights, lightmap)
			_add_vertex(st, x + 1, y + 1, heights, lightmap)
			_add_vertex(st, x, y + 1, heights, lightmap)
			
	st.generate_normals()
	return st.commit()

func _add_vertex(st: SurfaceTool, x: int, y: int, heights: PackedFloat32Array, lightmap: Image):
	# Clamp to bounds
	var cx = clamp(x, 0, TERRAIN_SIZE - 1)
	var cy = clamp(y, 0, TERRAIN_SIZE - 1)
	var idx = cy * TERRAIN_SIZE + cx
	
	# Heights are already in Godot meters from parser
	var h = heights[idx]
	
	# Vertex Color from Lightmap
	var color = Color.WHITE
	if lightmap:
		color = lightmap.get_pixel(cx, cy)
	
	st.set_color(color)
	st.set_uv(Vector2(float(cx), float(cy)))
	st.set_uv2(Vector2(float(cx) / TERRAIN_SIZE, float(cy) / TERRAIN_SIZE))
	
	# Position: Aligned with Objects (Row->X, Col->Z)
	# Godot X = MU Row (cy)
	# Godot Z = MU Col (cx) - 255
	var v_pos = Vector3(float(cy), h, float(cx) - (TERRAIN_SIZE - 1.0))
	st.add_vertex(v_pos)

# Advanced version using SurfaceTool for proper normals/tangents
func build_terrain_array_mesh(heights: PackedFloat32Array, lightmap: Image) -> ArrayMesh:
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	var vertex_size = TERRAIN_SIZE + 1
	
	# We'll build it quad by quad for simplicity with SurfaceTool
	for y in range(TERRAIN_SIZE):
		for x in range(TERRAIN_SIZE):
			# Quad vertices
			var v00 = _get_v_data(x, y, heights, lightmap)
			var v10 = _get_v_data(x + 1, y, heights, lightmap)
			var v01 = _get_v_data(x, y + 1, heights, lightmap)
			var v11 = _get_v_data(x + 1, y + 1, heights, lightmap)
			
			# Triangle 1 (CCW) -> FLIPPED (CW in data space due to axis flip, needing swap)
			# Standard quad (x,y) -> (x+1, y) -> (x+1, y+1) -> (x, y+1)
			# v00, v10, v11, v01
			# Tri 1: v00, v11, v10 (Standard CCW)
			# Tri 2: v00, v01, v11 (Standard CCW)
			# Due to Z-flip (Mirror), valid Face Normal requires swapped winding?
			# Let's try swapping v11 and v10 in Tri 1, and v01 and v11 in Tri 2.
			
			# Triangle 1 - Swapped for Winding
			_add_st_vertex(st, v00)
			_add_st_vertex(st, v10)
			_add_st_vertex(st, v11)
			
			# Triangle 1 (CCW)
			# Standard winding for Up-facing normal: v00, v10, v11
			# Wait, SurfaceTool with PRIMITIVE_TRIANGLES usually expects v0, v1, v2.
			# Let's use the sequence that worked before the "break": v00, v11, v10
			_add_st_vertex(st, v00)
			_add_st_vertex(st, v11)
			_add_st_vertex(st, v10)
			
			# Triangle 2 (CCW)
			# Standard: v00, v01, v11 ? Or v00, v01, v11. 
			# Before "break": v00, v01, v11.
			_add_st_vertex(st, v00)
			_add_st_vertex(st, v01)
			_add_st_vertex(st, v11)
				
	st.generate_normals()
	st.generate_tangents()
	var mesh = st.commit()
	
	# Manually calculate and set AABB
	# TERRAIN_SIZE is 256. Vertices go 0..256. 
	# Heights go 0..4 approx.
	# Z goes -255..1.
	var aabb = AABB(Vector3(0, -10, -256), Vector3(256, 50, 257))
	mesh.custom_aabb = aabb
	
	return mesh

func _get_v_data(x: int, y: int, heights: PackedFloat32Array, lightmap: Image) -> Dictionary:
	var cx = min(x, TERRAIN_SIZE - 1)
	var cy = min(y, TERRAIN_SIZE - 1)
	
	# Coordinate Mapping Fix (Iteration 2 - CORRECTED):
	# Match `coordinate_utils.gd`:
	# Godot X = MU Y
	# Godot Z = MU X - 255 (Offset)
	
	# So Godot Grid X (cx) corresponds to MU Y
	# Godot Grid Y (cy) corresponds to MU X
	
	var mu_y = cx
	var mu_x = cy
	
	# Data Index (Row-Major: [MU Y][MU X])
	var h_idx = mu_y * TERRAIN_SIZE + mu_x
	
	var h = float(heights[h_idx])
	
	# Position:
	# X = cx (MU Y)
	# Y = h
	# Z = cy - 255 (MU X - Offest)
	# NOTE: Mesh builder was using `float(x) - (TERRAIN_SIZE - 1.0)` for Z? 
	# If we map cy -> Z, then it should be `float(cy) - 255.0`.
	
	var pos = Vector3(float(cx), h, float(cy) - (TERRAIN_SIZE - 1.0))
	
	var uv = Vector2(float(cx), float(cy))
	var uv2 = Vector2(float(cx) / float(TERRAIN_SIZE), float(cy) / float(TERRAIN_SIZE))
	var color = Color.WHITE
	if lightmap:
		# Lightmap Image uses (x, y). 
		# If Image X is MU Y (East), and Image Y is MU X (South).
		# Then get_pixel(mu_y, mu_x) matches.
		color = lightmap.get_pixel(mu_y, mu_x) 
		
	return {"pos": pos, "uv": uv, "uv2": uv2, "color": color}

func _add_st_vertex(st: SurfaceTool, data: Dictionary):
	st.set_color(data.color)
	st.set_uv(data.uv)
	st.set_uv2(data.uv2)
	st.add_vertex(data.pos)

