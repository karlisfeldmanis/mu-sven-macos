@tool
extends SceneTree

func _init():
	print("\n[Verification] Checking Terrain Material Properties...")
	var heightmap = MUHeightmap.new()
	heightmap.world_id = 0 # Lorencia
	# We need to add it to the tree for _ready to fire and load assets
	root.add_child(heightmap)
	
	# Wait for loading to complete (it's synchronous in load_heightmap)
	# load_heightmap is called in _ready
	
	var mi = heightmap.get_node("TerrainMesh")
	if not mi:
		print("[FAIL] TerrainMesh instance not found.")
		quit(1)
		return
		
	var mat = mi.material_override as ShaderMaterial
	if not mat:
		print("[FAIL] ShaderMaterial not found on TerrainMesh.")
		quit(1)
		return
		
	print("[OK] ShaderMaterial found.")
	
	var sym_tex = mat.get_shader_parameter("symmetry_map")
	if sym_tex:
		print("[OK] symmetry_map parameter is set.")
		if sym_tex is ImageTexture:
			var img = sym_tex.get_image()
			if img:
				print("[OK] symmetry_map image is valid (%dx%d)." % [img.get_width(), img.get_height()])
				# Check if there are any non-zero pixels (symmetry data)
				var has_data = false
				for y in range(img.get_height()):
					for x in range(img.get_width()):
						if img.get_pixel(x, y).r > 0:
							has_data = true
							break
					if has_data: break
				if has_data:
					print("[OK] symmetry_map contains non-zero data (SVEN parity).")
				else:
					print("[WARNING] symmetry_map is all zeros. Is this expected for World 0?")
			else:
				print("[FAIL] symmetry_map has no image data.")
	else:
		print("[FAIL] symmetry_map parameter is MISSING.")
		
	var lut = mat.get_shader_parameter("terrain_data_lut")
	if lut:
		print("[OK] terrain_data_lut parameter is set.")
		
	quit(0)
