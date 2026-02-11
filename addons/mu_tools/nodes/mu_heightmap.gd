extends Node3D

## MUHeightmap
## Standardized node for rendering MU terrain heightmaps using the Unified API.

class_name MUHeightmap

@export var world_id: int = 1:
	set(val):
		world_id = val
		if is_inside_tree():
			load_heightmap()

@export var data_path: String = "res://reference/MuMain/src/bin/Data":
	set(val):
		data_path = val
		if is_inside_tree():
			load_heightmap()

var _api: Node # Using Node as base type to avoid circular preloads if any
var _terrain_mi: MeshInstance3D
var _heightmap_data: PackedFloat32Array
var _mapping_data: Variant
var _objects_data: Array = []

func _ready():
	_api = load("res://addons/mu_tools/core/mu_api.gd").new()
	load_heightmap()

func load_heightmap():
	# Clear existing
	if _terrain_mi:
		_terrain_mi.queue_free()
		_terrain_mi = null
		
	print("[MUHeightmap] Loading World %d..." % world_id)
	
	# 1. Load Data
	var world_data = _api.data().load_world_data(world_id, data_path)
	if world_data.is_empty():
		push_error("[MUHeightmap] Failed to load world data for World %d" % world_id)
		return
		
	_heightmap_data = world_data.heightmap
	_mapping_data = world_data.mapping
	_objects_data = world_data.objects

	# 2. Setup Environment
	_api.render().setup_world_environment(self, world_id)

	# 3. Build Mesh
	var world_dir = "World" + str(world_id + 1)
	var light_path = data_path.path_join(world_dir).path_join("TerrainLight.OZJ")
	if not FileAccess.file_exists(light_path):
		light_path = data_path.path_join(world_dir).path_join("TerrainLight.OZB")
	
	var lightmap: Image = null
	var l_tex = _api.render().load_mu_texture(ProjectSettings.globalize_path(light_path))
	if l_tex:
		lightmap = l_tex.get_image()
	
	var mesh = _api.mesh().build_terrain_mesh(_heightmap_data, lightmap)
	_terrain_mi = MeshInstance3D.new()
	_terrain_mi.name = "TerrainMesh"
	_terrain_mi.mesh = mesh
	add_child(_terrain_mi)
	_terrain_mi.create_trimesh_collision()

	# 4. Setup Material
	_setup_material(_mapping_data, l_tex, world_data.attributes.symmetry)

func _setup_material(
	map_data: MUTerrainParser.MapData, 
	_l_tex: Texture2D, 
	symmetry_data: PackedByteArray
):
	var textures: Array[Image] = []
	for i in range(256):
		var p = Image.create(256, 256, true, Image.FORMAT_RGBA8)
		p.fill(Color.BLACK)
		p.generate_mipmaps()
		textures.append(p)
		
	# Load base textures from World directory
	var world_dir = "World" + str(world_id + 1)
	var base_world_path = data_path.path_join(world_dir)
	
	var texture_cache = {} # idx -> { priority, path }
	# Priority: .ozj=3, .ozt=2, .tga=1, .jpg=0
	
	var dir = DirAccess.open(base_world_path)
	if dir:
		dir.list_dir_begin()
		var file_name = dir.get_next()
		while file_name != "":
			if not dir.current_is_dir():
				var lower = file_name.to_lower()
				var idx = -1
				
				var base_name = lower.get_basename()
				
				if base_name == "tilegrass01": idx = 0
				elif base_name == "tilegrass02": idx = 1
				elif base_name == "tileground01": idx = 2
				elif base_name == "tileground02": idx = 3
				elif base_name == "tileground03": idx = 4
				elif base_name == "tilewater01": idx = 5
				elif base_name == "tilewood01": idx = 6
				elif base_name == "tilerock01": idx = 7
				elif base_name == "tilerock02": idx = 8
				elif base_name == "tilerock03": idx = 9
				elif base_name == "tilerock04": idx = 10
				elif base_name == "tilerock05": idx = 11
				elif base_name == "tilerock06": idx = 12
				elif base_name == "tilerock07": idx = 13
				elif lower.begins_with("exttile"):
					var num_str = lower.trim_prefix("exttile").get_basename()
					idx = 14 + (num_str.to_int() - 1)
				
				if idx >= 0 and idx < 256:
					var priority = 0
					if lower.ends_with(".ozj"): priority = 3
					elif lower.ends_with(".ozt"): priority = 2
					elif lower.ends_with(".tga"): priority = 1
					elif lower.ends_with(".jpg"): priority = 0
					
					if not texture_cache.has(idx) or priority > texture_cache[idx].priority:
						texture_cache[idx] = { 
							"priority": priority, 
							"path": base_world_path.path_join(file_name) 
						}
			file_name = dir.get_next()
 
	var idx_to_name = {}
	var original_widths = {}
	# Now load the cached unique indices
	for idx in texture_cache:
		var tex_path = texture_cache[idx].path
		var tex = _api.render().load_mu_texture(ProjectSettings.globalize_path(tex_path))
		if tex:
			var img = tex.get_image()
			if img:
				idx_to_name[idx] = tex_path.get_file()
				img.convert(Image.FORMAT_RGBA8)
				original_widths[idx] = img.get_width()
				if img.get_size() != Vector2i(256, 256):
					img.resize(256, 256)
				img.generate_mipmaps()
				textures[idx] = img
 
	var tex_array = Texture2DArray.new()
	tex_array.create_from_images(textures)
 
	# Helper maps for shader
	var l1_img = Image.create(256, 256, false, Image.FORMAT_R8)
	var l2_img = Image.create(256, 256, false, Image.FORMAT_R8)
	var alpha_img = Image.create(256, 256, false, Image.FORMAT_R8)
	var grass_off_img = Image.create(256, 1, false, Image.FORMAT_R8)
	
	# Generate Grass Offset (Authentic MU Randomized Row Shift)
	# MU uses (rand() % 4) / 4.0 shift for every terrain row to break grid lines.
	var rng = RandomNumberGenerator.new()
	rng.seed = 42 # Keep it stable
	for y in range(256):
		var off = float(rng.randi() % 4) * 0.25
		grass_off_img.set_pixel(y, 0, Color(off, 0, 0, 1.0))
 
	var non_zero_alpha = 0
	for y in range(256):
		for x in range(256):
			var tile_idx = y * 256 + x
			l1_img.set_pixel(x, y, Color(float(map_data.layer1[tile_idx])/255.0, 0, 0, 1.0))
			l2_img.set_pixel(x, y, Color(float(map_data.layer2[tile_idx])/255.0, 0, 0, 1.0))
			var a = map_data.alpha[tile_idx]
			alpha_img.set_pixel(x, y, Color(a, 0, 0, 1.0))
			if a > 0.01: non_zero_alpha += 1
  
	print("[MUHeightmap] Alpha Mask populated with %d non-zero pixels" % non_zero_alpha)
 
	var l1_tex = ImageTexture.create_from_image(l1_img)
	var l2_tex = ImageTexture.create_from_image(l2_img)
	var a_tex = ImageTexture.create_from_image(alpha_img)
	var g_tex = ImageTexture.create_from_image(grass_off_img)
	
	# Symmetry Map (SVEN Rotation Parity)
	var s_img = Image.create(256, 256, false, Image.FORMAT_R8)
	var raw_sym = symmetry_data
	if raw_sym.size() == 256 * 256:
		for y in range(256):
			for x in range(256):
				s_img.set_pixel(x, y, Color(float(raw_sym[y * 256 + x])/255.0, 0, 0, 1.0))
	var sym_tex = ImageTexture.create_from_image(s_img)
	
	# SVEN logic: Scale = 64.0 / BitmapWidth
	var scale_map = PackedFloat32Array()
	scale_map.resize(256)
	scale_map.fill(1.0)
	
	var category_map = PackedFloat32Array()
	category_map.resize(256)
	category_map.fill(0.0)
	
	var symmetry_map = PackedFloat32Array()
	symmetry_map.resize(256); symmetry_map.fill(0.0)
	
	for i in range(textures.size()):
		var w = float(original_widths.get(i, textures[i].get_width()))
		if w > 0:
			var t_name = idx_to_name.get(i, "TileIndex_%d" % i).to_lower()
			var cat = _categorize_texture(t_name)
			category_map[i] = cat
			
			# 🟢 1:1 MIGRATION LOGIC (Legit MU Parity)
			# SVEN: Width = 64.0 / BitmapWidth
			if w > 0:
				scale_map[i] = 64.0 / w
			else:
				scale_map[i] = 1.0 # Fallback
				
			# Category 0: Discrete Detail (Roads) - Use map symmetry.
			# Category 4: Discrete Ground (City Floors) - Force Rotation (Sym 4) + Checkerboard.
			# Categories 1-3: Seamless Floors - Symmetry is ignored by shader.
			var actual_symmetry = 0
			if cat == 0.0:
				actual_symmetry = symmetry_data[i]
			elif cat == 4.0:
				actual_symmetry = 4 # Force Rotation
			
			category_map[i] = cat
			symmetry_map[i] = actual_symmetry
	
	# Explicit Water Fix
	scale_map[5] = 1.0; 
	category_map[5] = 2.0; 
	symmetry_map[5] = 0.0;
	
	var mat = _api.render().create_terrain_material(
		tex_array, l1_tex, l2_tex, a_tex, g_tex, _l_tex, 0,
		scale_map, symmetry_map, category_map, sym_tex
	)
	_terrain_mi.material_override = mat
	print("✓ MUHeightmap: 1:1 Migration Logic Active (City=Horizontal, Nature=Seamless).")

func _categorize_texture(tex_name: String) -> float:
	var n = tex_name.to_lower()
	
	# Category 1: Seamless Nature (Scale 0.25)
	if n.contains("grass") or n.contains("leaf") or n.contains("wood"): 
		return 1.0
		
	# Category 2: Water (Scale 1.0)
	if n.contains("water"): 
		return 2.0
		
	# Category 3: Seamless Floor (Scale 0.25)
	if n.contains("rock"): 
		return 3.0
		
	# Category 4: Discrete Ground (Scale 1.0)
	if n.contains("ground") or n.contains("stone") or n.contains("mstone"):
		return 4.0
		
	# Category 0: Discrete Detail / Roads (Scale 1.0)
	return 0.0

func get_height_data() -> PackedFloat32Array:
	return _heightmap_data

func get_objects_data() -> Array:
	return _objects_data

## Bilinear height query for world positions (Authentic MU Parity)
func get_height_at_world(world_pos: Vector3) -> float: 
	# Proven Transpose Mapping
	# Godot X = MU Y, Godot Z = MU X → inverse: MU X = G_Z, MU Y = G_X
	const TERRAIN_SIZE = 256
	var mu_x = world_pos.z
	var mu_y = world_pos.x
	
	# Bounds check
	if mu_x < 0 or mu_x >= TERRAIN_SIZE or mu_y < 0 or mu_y >= TERRAIN_SIZE:
		return 0.0
	
	# Get integer tile and decimal fraction
	var xi = int(mu_x)
	var yi = int(mu_y)
	var xd = mu_x - float(xi)
	var yd = mu_y - float(yi)
	
	# Clamp to valid sampling range
	xi = clampi(xi, 0, TERRAIN_SIZE - 2)
	yi = clampi(yi, 0, TERRAIN_SIZE - 2)
	
	# Sample 4 corners (Row-major: idx = y * SIZE + x)
	var idx1 = yi * TERRAIN_SIZE + xi           # (xi, yi)
	var idx2 = yi * TERRAIN_SIZE + (xi + 1)     # (xi+1, yi)  
	var idx3 = (yi + 1) * TERRAIN_SIZE + xi     # (xi, yi+1)
	var idx4 = (yi + 1) * TERRAIN_SIZE + (xi + 1)  # (xi+1, yi+1)
	
	if idx4 >= _heightmap_data.size():
		return 0.0
		
	var h1 = _heightmap_data[idx1]
	var h2 = _heightmap_data[idx2]
	var h3 = _heightmap_data[idx3]
	var h4 = _heightmap_data[idx4]
	
	# Interpolate along Y on left edge (xi), then right edge (xi+1)
	var left = h1 + (h3 - h1) * yd
	var right = h2 + (h4 - h2) * yd
	# Then interpolate along X
	return left + (right - left) * xd

func set_debug_mode(mode: int):
	if _terrain_mi and _terrain_mi.material_override:
		_terrain_mi.material_override.set_shader_parameter("debug_mode", mode)
	# Note: Global shader uniforms (mu_wind_speed, mu_water_move_uv, mu_world_time)
	# are handled by MUEnvironment._process() with SVEN-parity formulas.
