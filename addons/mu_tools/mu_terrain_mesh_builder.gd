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
			_add_vertex(st, x, y, heights, lightmap)
			_add_vertex(st, x + 1, y + 1, heights, lightmap) # Swapped
			_add_vertex(st, x + 1, y, heights, lightmap) # Swapped
			
			_add_vertex(st, x, y, heights, lightmap)
			_add_vertex(st, x, y + 1, heights, lightmap) # Swapped
			_add_vertex(st, x + 1, y + 1, heights, lightmap) # Swapped
			
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
	
	# Position: X and -Z are tile coordinates (already in meters), Y is height
	var v_pos = Vector3(float(cx), h, -float(cy))
	st.add_vertex(v_pos)

# Advanced version using ArrayMesh for better performance if needed
func build_terrain_array_mesh(heights: PackedFloat32Array, lightmap: Image) -> ArrayMesh:
	var vertices = PackedVector3Array()
	var colors = PackedColorArray()
	var uvs = PackedVector2Array()
	var uvs2 = PackedVector2Array()
	var indices = PackedInt32Array()
	
	vertices.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	colors.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	uvs.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	uvs2.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	
	for y in range(TERRAIN_SIZE):
		for x in range(TERRAIN_SIZE):
			var idx = y * TERRAIN_SIZE + x # Standard Row-Major (Data Index)
			
			# Transpose Mode 4 Mapping:
			# MU-Row (y) -> Godot X
			# MU-Col (x) -> Godot Z
			# Up (h)  -> Godot Y
			# Height Scale: 1.0x (Raw) for perfect parity with objects
			var h = float(heights[idx]) * 1.0
			vertices[idx] = Vector3(float(y), h, float(x) - (TERRAIN_SIZE - 1.0))
			
			# UV: Per-tile coordinates (Swapped to match Godot World X/Z)
			uvs[idx] = Vector2(float(y), float(x))
			
			# UV2: Global coordinates for index maps (X=Row=y, Y=Col=x)
			# Synchronized with (y, x) pixel placement in mu_terrain.gd
			uvs2[idx] = Vector2((float(y) + 0.5) / TERRAIN_SIZE, (float(x) + 0.5) / TERRAIN_SIZE)
			
			if lightmap:
				colors[idx] = lightmap.get_pixel(x, y) 
			else:
				colors[idx] = Color.WHITE
				
			if x < TERRAIN_SIZE - 1 and y < TERRAIN_SIZE - 1:
				var i = idx
				# Correct CCW Winding: (i, i + 256 + 1, i + 1) and (i, i + 256, i + 256 + 1)
				indices.append(i)
				indices.append(i + 1)
				indices.append(i + TERRAIN_SIZE + 1)
				
				indices.append(i)
				indices.append(i + TERRAIN_SIZE + 1)
				indices.append(i + TERRAIN_SIZE)
				
	var normals = PackedVector3Array()
	normals.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	normals.fill(Vector3.UP)
	
	var arr = []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = vertices
	arr[Mesh.ARRAY_COLOR] = colors
	arr[Mesh.ARRAY_NORMAL] = normals
	arr[Mesh.ARRAY_TEX_UV] = uvs
	arr[Mesh.ARRAY_TEX_UV2] = uvs2
	arr[Mesh.ARRAY_INDEX] = indices
	
	var mesh = ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	
	# Manually calculate and set AABB to ensure it's not culled
	var aabb = AABB(Vector3(0, -10, -256), Vector3(256, 120, 256))
	mesh.custom_aabb = aabb
	
	return mesh
