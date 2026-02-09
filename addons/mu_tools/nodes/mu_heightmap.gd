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

func _setup_material(map_data: MUTerrainParser.MapData, l_tex: Texture2D, symmetry_data: PackedByteArray):
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
				
				if lower.begins_with("tilegrass01"): idx = 0
				elif lower.begins_with("tilegrass02"): idx = 1
				elif lower.begins_with("tileground01"): idx = 2
				elif lower.begins_with("tileground02"): idx = 3
				elif lower.begins_with("tileground03"): idx = 4
				elif lower.begins_with("tilewater01"): idx = 5
				elif lower.begins_with("tilewood01"): idx = 6
				elif lower.begins_with("tilerock01"): idx = 7
				elif lower.begins_with("tilerock02"): idx = 8
				elif lower.begins_with("tilerock03"): idx = 9
				elif lower.begins_with("tilerock04"): idx = 10
				elif lower.begins_with("tilerock05"): idx = 11
				elif lower.begins_with("tilerock06"): idx = 12
				elif lower.begins_with("tilerock07"): idx = 13
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
 
	for y in range(256):
		for x in range(256):
			var tile_idx = y * 256 + x
			l1_img.set_pixel(x, y, Color(float(map_data.layer1[tile_idx])/255.0, 0, 0, 1.0))
			l2_img.set_pixel(x, y, Color(float(map_data.layer2[tile_idx])/255.0, 0, 0, 1.0))
			alpha_img.set_pixel(x, y, Color(map_data.alpha[tile_idx], 0, 0, 1.0))
 
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
			scale_map[i] = 64.0 / w
			var t_name = idx_to_name.get(i, "TileIndex_%d" % i)
			category_map[i] = _categorize_texture(t_name)
			
	# 1. SVEN Terrain Parity (Dynamic Metadata)
	# Logic:
	# - Scale = 64.0 / texture_width (Normalizing to 64px virtual tiles)
	# - Category = Derived from filename (Nature vs Stone)
	# - Symmetry = Default to None (0.0) unless nature logic requires otherwise
	
	# Scale/Category/Symmetry maps are already populated above.
	# We just ensure water (Index 5) is correctly categorized and scaled for flow animation.
	scale_map[5] = 1.0; 
	category_map[5] = 2.0; 
	symmetry_map[5] = 0.0;
	
	var mat = _api.render().create_terrain_material(
		tex_array, l1_tex, l2_tex, a_tex, g_tex, l_tex, 0,
		scale_map, symmetry_map, category_map, sym_tex
	)
	_terrain_mi.material_override = mat
	print("✓ MUHeightmap: Hybrid Seamless Active (Grass=Nature, Stone=Mirror).")

func _categorize_texture(tex_name: String) -> float:
	var n = tex_name.to_lower()
	if n.contains("grass"): return 1.0
	if n.contains("water"): return 2.0
	if n.contains("wood") or n.contains("stone") or \
	   n.contains("ground02") or n.contains("ground03"): 
		return 3.0
	if n.contains("ground01"): return 4.0
	return 0.0

func set_debug_mode(mode: int):
	if _terrain_mi and _terrain_mi.material_override:
		_terrain_mi.material_override.set_shader_parameter("debug_mode", mode)

func _process(_delta: float):
	# SVEN Parity: Update global time for shaders
	var time = Time.get_ticks_msec() / 1000.0
	# Standard global uniforms for MU shaders
	RenderingServer.global_shader_parameter_set("mu_world_time", time)
	# Water flow (64.0 / texture_width * WaterMove)
	# Standard water move is roughly 0.001 per frame at 30fps
	var water_move = time * 0.05 
	RenderingServer.global_shader_parameter_set("mu_water_move_uv", water_move)
