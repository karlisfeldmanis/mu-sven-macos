extends Node3D

class_name MUTerrain

const TERRAIN_SIZE = 256

const LORENCIA_OBJECT_MAP = {
	0: "Tree%02d",	   # 0-12
	20: "Grass%02d",	  # 20-27
	30: "Stone%02d",	  # 30-34
	40: "StoneStatue%02d", # 40-42
	43: "SteelStatue01",
	44: "Tomb%02d",	   # 44-46
	50: "FireLight%02d",  # 50-51
	52: "Bonfire01",
	55: "DoungeonGate01",
	56: "MerchantAnimal%02d", # 56-57
	58: "TreasureDrum01",
	59: "TreasureChest01",
	60: "Ship01",
	65: "SteelWall%02d",  # 65-67
	68: "SteelDoor01",
	69: "StoneWall%02d",  # 69-74
	75: "StoneMuWall%02d", # 75-78
	80: "Bridge01",
	81: "Fence%02d",	  # 81-84
	85: "BridgeStone01",
	90: "StreetLight01",
	91: "Cannon%02d",	 # 91-93
	95: "Curtain01",
	96: "Sign%02d",	   # 96-97
	98: "Carriage%02d",   # 98-101
	102: "Straw%02d",	 # 102-103
	105: "Waterspout01",
	106: "Well%02d",	  # 106-109
	110: "Hanging01",
	111: "Stair01",
	115: "House%02d",	 # 115-119
	120: "Tent01",
	121: "HouseWall%02d", # 121-126
	127: "HouseEtc%02d",  # 127-129
	130: "Light%02d",	 # 130-132
	133: "PoseBox01",
	140: "Furniture%02d", # 140-146
	150: "Candle01",
	151: "Beer%02d"	   # 151-153
}

@export var world_id: int = 1
@export var data_path: String = "res://reference/MuMain/src/bin/Data/World1"

const MUFireScript = preload("res://scenes/lorencia_effects/mu_fire.gd")

const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUTerrainMeshBuilder = preload("res://addons/mu_tools/mu_terrain_mesh_builder.gd")
const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")
const MUPreflight = preload("res://addons/mu_tools/mu_preflight.gd")
const MUEnvironmentScript = preload("res://addons/mu_tools/mu_environment.gd")

var env_controller: Node
var heightmap: PackedFloat32Array
var map_data: MUTerrainParser.MapData
var attributes: PackedByteArray
var lightmap: Image # TerrainLight.OZJ RGB lightmap
var object_data: Array # Array[MUTerrainParser.ObjectData]
var terrain_mesh: MeshInstance3D
var water_mesh: MeshInstance3D
var grass_mesh: MultiMeshInstance3D

func load_world():
	var parser = MUTerrainParser.new()
	var mesh_builder = MUTerrainMeshBuilder.new()
	
	# 1. Load Data
	# 1. Load Data
	var world_dir = "World" + str(world_id)
	
	# Determine if data_path already includes the world directory
	var base_data_path = data_path
	if data_path.get_file() == world_dir:
		base_data_path = data_path.get_base_dir()
	
	var world_full_path = base_data_path.path_join(world_dir)
	var height_file = world_full_path.path_join("TerrainHeight.OZB")
	var mapping_file = world_full_path.path_join("EncTerrain" + str(world_id) + ".map")
	var objects_file = world_full_path.path_join("EncTerrain" + str(world_id) + ".obj")
	var att_file = world_full_path.path_join("EncTerrain" + str(world_id) + ".att")
	var light_file = world_full_path.path_join("TerrainLight.OZJ")
	print("[MUTerrain] Loading World %d from %s..." % [world_id, world_full_path])
	
	# 0. Pre-flight Check
	var preflight = MUPreflight.validate_world(world_id, data_path, LORENCIA_OBJECT_MAP)
	if not preflight.success:
		push_error("[MUTerrain] Pre-flight FAILED for World %d" % world_id)
		for err in preflight.errors:
			print("  [Pre-flight ERROR] ", err)
	
	if not preflight.warnings.is_empty():
		for warn in preflight.warnings:
			print("  [Pre-flight WARNING] ", warn)
		
	print("[MUTerrain] Pre-flight summary: %d objects, %d missing BMDs, %d missing textures" % [
		preflight.total_objects, preflight.missing_bmds, preflight.missing_textures
	])
	
	heightmap = parser.parse_height_file(height_file)
	map_data = parser.parse_mapping_file(mapping_file)
	object_data = parser.parse_objects_file(objects_file)
	
	# 4. Load Attributes
	attributes = parser.parse_attributes_file(att_file)
	print("[MUTerrain] Loaded attributes: %d bytes" % attributes.size())
	
	# 5. Load Lightmap (TerrainLight.OZJ)
	var light_tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(light_file))
	if light_tex:
		lightmap = light_tex.get_image()
		print("[MUTerrain] Loaded lightmap: %dx%d" % [lightmap.get_width(), lightmap.get_height()])
	else:
		print("[MUTerrain] WARNING: Failed to load TerrainLight.OZJ, using white lighting")
		lightmap = null
	
	# 3. Build Mesh
	var mesh = mesh_builder.build_terrain_array_mesh(heightmap, lightmap)
	
	terrain_mesh = MeshInstance3D.new()
	terrain_mesh.mesh = mesh
	print("[MUTerrain] Created terrain mesh with %d surfaces" % 
			terrain_mesh.mesh.get_surface_count())
	add_child(terrain_mesh)
	
	# 6. Setup Material
	_setup_material()
	
	# 7. Create Water Mesh
	_create_water_mesh()
	
	# 8. Create Grass Mesh
	_create_grass_mesh()
	
	_spawn_objects()
	
	# 9. Initialize Environment Controller
	if not env_controller:
		env_controller = MUEnvironmentScript.new()
		env_controller.name = "MUEnvironment"
		add_child(env_controller)
		
	print("[MUTerrain] World load complete with objects and environment.")


func _setup_material():
	print("[MUTerrain] Loading tile textures...")
	
	# 1. Base textures (Indices 0-13)
	var base_texture_names = [
		"TileGrass01",	# Index 0
		"TileGrass02",	# Index 1
		"TileGround01",   # Index 2
		"TileGround02",   # Index 3
		"TileGround03",   # Index 4
		"TileWater01",	# Index 5 (Restored as Water, now with Nearest filtering)
		"TileWood01",	 # Index 6
		"TileRock01",	 # Index 7
		"TileRock02",	 # Index 8
		"TileRock03",	 # Index 9
		"TileRock04",	 # Index 10
		"TileRock05",	 # Index 11
		"TileRock06",	 # Index 12
		"TileRock07",	 # Index 13
	]
	
	# 2. Map-specific ExtTile textures (Indices 14-255)
	# MuOnline usually loads ExtTile01 to ExtTileXY and maps them to BITMAP_MAPTILE + 13 + i
	# However, we'll try to load any available ExtTile textures up to 242 (255 - 13)
	var texture_map = {} # index -> Image
	
	
	# Load base textures
	for i in range(base_texture_names.size()):
		var tex_name = base_texture_names[i]
		var img = _load_tile_image(tex_name)
		if img:
			texture_map[i] = img
			print("  [%d] Loaded base: %s" % [i, tex_name])

	# Load extended textures (ExtTile01, ExtTile02, etc.)
	# Mapping starts at index 14
	for i in range(1, 243):
		var tex_name = "ExtTile%02d" % i
		var img = _load_tile_image(tex_name)
		if img:
			var target_idx = 13 + i
			if target_idx < 256:
				texture_map[target_idx] = img
				print("  [%d] Loaded extended: %s" % [target_idx, tex_name])

	# Create fallbacks and finalize the list of 256 images
	var final_images: Array[Image] = []
	var max_size = 256
	
	# Initial pass to find max size
	for img in texture_map.values():
		max_size = max(max_size, max(img.get_width(), img.get_height()))
	
	# Create fallback image
	var fallback_img = Image.create(max_size, max_size, false, Image.FORMAT_RGB8)
	fallback_img.fill(Color(0.0, 0.0, 0.0)) # Black fallback to avoid purple artifacts
	
	print("[MUTerrain] Starting texture array assembly...")
	for i in range(256):
		var img: Image = texture_map.get(i)
		if img:
			if img.get_width() != max_size or img.get_height() != max_size:
				img.resize(max_size, max_size, Image.INTERPOLATE_LANCZOS)
			final_images.append(img)
		else:
			final_images.append(fallback_img)
	
	print("[MUTerrain] Creating Texture2DArray with 256 layers...")
	var tex_array = Texture2DArray.new()
	var err = tex_array.create_from_images(final_images)
	if err != OK:
		push_error("[MUTerrain] FAILED to create Texture2DArray: ", err)
	else:
		print("[MUTerrain] Texture2DArray created successfully!")
	
	# Create material
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/mu_terrain.gdshader")
	mat.set_shader_parameter("tile_textures", tex_array)
	
	# Pass layer data to shader
	if map_data:
		var layer1_img = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		var layer2_img = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		var alpha_img = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		for y in range(TERRAIN_SIZE):
			for x in range(TERRAIN_SIZE):
				# Shader Map Indexing: (X=Row, Y=Col)
				# Matches Transpose mapping: MU-Row (y) -> Godot X, MU-Col (x) -> Godot Z
				var idx = y * TERRAIN_SIZE + x 
				
				# Use set_pixel(y, x) to match Grid(Row, Col) where Coll (x) maps to Mesh Z (MU X)
				# Visual X (Pixel X) = MU Y
				# Visual Y (Pixel Y) = MU X
				var pixel_y = x
				
				layer1_img.set_pixel(y, pixel_y, Color(map_data.layer1[idx] / 255.0, 0, 0))
				layer2_img.set_pixel(y, pixel_y, Color(map_data.layer2[idx] / 255.0, 0, 0))
				alpha_img.set_pixel(y, pixel_y, Color(map_data.alpha[idx], 0, 0))
		
		mat.set_shader_parameter("layer1_map", ImageTexture.create_from_image(layer1_img))
		mat.set_shader_parameter("layer2_map", ImageTexture.create_from_image(layer2_img))
		mat.set_shader_parameter("alpha_map", ImageTexture.create_from_image(alpha_img))
		
		# SVEN compatibility: Generate grass UV randomization texture
		# TerrainGrassTexture[yi] - indexed by Y only (per-row, not per-pixel!)
		var grass_offset_img = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		for y in range(TERRAIN_SIZE):
			var random_offset = float(randi() % 4) / 4.0  # 0.0, 0.25, 0.5, 0.75
			
			# NOTE: Grass offset was random per MU-row (y). 
			# In our Texture Map, MU-row (y) is the X axis.
			# So we want stripes along the Y axis (constant X) or stripes along X axis (constant Y)?
			# Logic: "TerrainGrassTexture[yi]" where yi is MU Y.
			# So for a given MU Y (Pixel X), the offset is constant across all MU X (Pixel Y).
			# So we fill columns with the same value.
			
			for x in range(TERRAIN_SIZE):
				var pixel_y = x
				grass_offset_img.set_pixel(y, pixel_y, Color(random_offset, 0, 0))
				
		var offset_tex = ImageTexture.create_from_image(grass_offset_img)
		mat.set_shader_parameter("grass_offset_map", offset_tex)
	
	terrain_mesh.material_override = mat

func _load_tile_image(tex_name: String) -> Image:
	var world_dir = "World" + str(world_id)
	var extensions = [".OZJ", ".ozj", ".OZT", ".ozt", ".jpg", ".JPG", ".tga", ".TGA"]
	
	# Determine if data_path already includes the world directory
	var base_data_path = data_path
	if data_path.get_file() == world_dir:
		base_data_path = data_path.get_base_dir()
	
	for ext in extensions:
		# 1. Try World-specific folder (Data/WorldX/ExtTileXX)
		var world_path = base_data_path.path_join(world_dir).path_join(tex_name + ext)
		var abs_world_path = ProjectSettings.globalize_path(world_path)
		if FileAccess.file_exists(abs_world_path):
			var direct_tex = MUTextureLoader.load_mu_texture(abs_world_path)
			if direct_tex:
				return direct_tex.get_image()
			
		# 2. Try global Data folder (Data/ExtTileXX)
		var global_path = base_data_path.path_join(tex_name + ext)
		var abs_global_path = ProjectSettings.globalize_path(global_path)
		if FileAccess.file_exists(abs_global_path):
			var direct_tex = MUTextureLoader.load_mu_texture(abs_global_path)
			if direct_tex:
				return direct_tex.get_image()
			
	# If we reach here, no file was found. 
	# For ExtTiles, this is expected if they don't exist, so we don't warn unless it's a base tile.
	if not tex_name.begins_with("ExtTile"):
		push_warning("[MUTerrain] ALL attempts failed to load tile: " + tex_name)
	return null


func _create_water_mesh():
	if not map_data or map_data.water_tiles.is_empty():
		return
	
	print("[MUTerrain] Creating water mesh with %d tiles..." % map_data.water_tiles.size())
	
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	for tile in map_data.water_tiles:
		var x = tile.x
		var y = tile.y
		
		# Get height at this position
		var idx = y * TERRAIN_SIZE + x
		var h = heightmap[idx] if idx < heightmap.size() else 0.0
		
		# Raise water slightly above terrain
		h += 0.15
		
		# Create water quad (two triangles)
		# Create water quad (two triangles)
		# Aligned with New Terrain & Objects:
		# Godot X = MU Row (y)
		# Godot Z = MU Col (x) - 255
		var v0 = Vector3(float(y), h, float(x) - (TERRAIN_SIZE - 1.0))
		var v1 = Vector3(float(y) + 1.0, h, float(x) - (TERRAIN_SIZE - 1.0))
		var v2 = Vector3(float(y) + 1.0, h, float(x) - (TERRAIN_SIZE - 1.0) + 1.0)
		var v3 = Vector3(float(y), h, float(x) - (TERRAIN_SIZE - 1.0) + 1.0)
		
		# Sample heights for all 4 corners for better leveling (or just use center)
		# For now, stay simple but aligned
		
		var uv0 = Vector2(0, 0)
		var uv1 = Vector2(1, 0)
		var uv2 = Vector2(1, 1)
		var uv3 = Vector2(0, 1)
		
		var normal = Vector3.UP
		var color = Color.WHITE  # White vertex color for proper lighting
		
		# Triangle 1 (v0, v2, v1 for CCW)
		st.set_normal(normal)
		st.set_color(color)
		st.set_uv(uv0)
		st.add_vertex(v0)
		st.set_uv(uv2)
		st.add_vertex(v2)
		st.set_uv(uv1)
		st.add_vertex(v1)
		
		# Triangle 2 (v0, v3, v2 for CCW)
		st.set_uv(uv0)
		st.add_vertex(v0)
		st.set_uv(uv3)
		st.add_vertex(v3)
		st.set_uv(uv2)
		st.add_vertex(v2)
	
	var mesh = st.commit()
	
	# Load water texture - try multiple paths
	var water_tex: Texture2D = null
	
	# Determine if data_path already includes the world directory
	var world_dir = "World" + str(world_id)
	var base_data_path = data_path
	if data_path.get_file() == world_dir:
		base_data_path = data_path.get_base_dir()
		
	var tex_paths = [
		base_data_path.path_join(world_dir).path_join("TileWater01.OZJ"),
		base_data_path.path_join(world_dir).path_join("TileWater01.OZT"),
		base_data_path.path_join("TileWater01.OZJ"), # Try root data dir too
		base_data_path.path_join("TileWater01.OZT"),
	]
	
	for path in tex_paths:
		var tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(path))
		if tex:
			water_tex = tex
			print("  Loaded water texture: %s" % path)
			break
	
	# Create water material with animated shader
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/water_shader.gdshader")
	mat.render_priority = 1  # Render after terrain
	if water_tex:
		mat.set_shader_parameter("water_texture", water_tex)
	else:
		print("  Using fallback blue color for water")
	

	water_mesh = MeshInstance3D.new()
	water_mesh.mesh = mesh
	water_mesh.material_override = mat
	water_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(water_mesh)
	
	print("[MUTerrain] Water mesh created!")

func _create_grass_mesh():
	if not map_data or map_data.grass_tiles.is_empty():
		return
	
	print("[MUTerrain] Creating grass with %d tiles..." % map_data.grass_tiles.size())
	
	# 1:1 Sven Diagonal Quad Geometry
	# Quad stands on the diagonal between tile corner 0 (0,0) and corner 2 (1,1)
	# Top vertices are shifted -0.5 on Sven X (Sven: -50.0 / 100.0)
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# MU Terrain Grass Geometry (Strict Sven Migration)
	# Bottom Vertices (Diagonal corner-to-corner)
	var v_bot_0 = Vector3(0, 0, 0) # sx, sy
	var v_bot_1 = Vector3(1, 0, 1) # sx + 1.0, sy + 1.0
	
	# Top Vertices (Shifted by 0.5 MU units (50.f) for "lean")
	# Sven: TerrainVertex[0][0] += -50.f; (MU X maps to Godot Z)
	var lean = -0.5
	var h = 1.28 # Sven: pBitmap->Height * 2.f / 100.0 (64 * 2 / 100)
	var v_top_0 = Vector3(0, h, lean)
	var v_top_1 = Vector3(1.0, h, 1.0 + lean)
	
	# UVs: We use a 4x1 atlas. Shader handles su += xf * 0.25
	# We just need to pass a random 0.0, 0.25, 0.5, 0.75 offset in COLOR.r
	
	# Vertex Order: v0, v1, v2, v3 (Sven glBegin(GL_QUADS))
	# v0 = top_0, v1 = top_1, v2 = bot_1, v3 = bot_0
	
	# Triangle 1 (top_0, top_1, bot_1)
	st.set_uv(Vector2(0, 0)); st.add_vertex(v_top_0)
	st.set_uv(Vector2(1, 0)); st.add_vertex(v_top_1)
	st.set_uv(Vector2(1, 1)); st.add_vertex(v_bot_1)
	
	# Triangle 2 (top_0, bot_1, bot_0)
	st.set_uv(Vector2(0, 0)); st.add_vertex(v_top_0)
	st.set_uv(Vector2(1, 1)); st.add_vertex(v_bot_1)
	st.set_uv(Vector2(0, 1)); st.add_vertex(v_bot_0)
		
	var grass_primitive_mesh = st.commit()
	
	# Iterate over grass types
	for grass_type in map_data.grass_tiles.keys():
		var positions = map_data.grass_tiles[grass_type]
		if positions.is_empty():
			continue
			
		print("[MUTerrain] Generating %d grass blades for Type %d..." % [
			positions.size(), grass_type
		])
		
		# Load specific texture for this type
		# Sven: BITMAP_MAPGRASS + TerrainMappingLayer1[Index]
		# World1 (Lorencia) uses TileGrass01.tga
		var tex_name = "TileGrass%02d" % (grass_type + 1)
		var grass_tex: Texture2D = null
		
		# Search for texture (TGA preferred for grass)
		var extensions = [".tga", ".TGA", ".ozj", ".OZJ", ".ozt", ".OZT", ".jpg"]
		var base_data_path = data_path.get_base_dir() # ../Data/
		var world_dir = "World" + str(world_id) # World1
		
		for ext in extensions:
			# Try World-specific path first
			var sub_path = world_dir.path_join(tex_name + ext)
			var path_check = base_data_path.path_join(sub_path)
			var actual_path = _find_case_insensitive_path(path_check)
			
			if actual_path != "":
				var loaded = MUTextureLoader.load_mu_texture(actual_path)
				if loaded:
					grass_tex = loaded
					print("  Loaded grass texture: %s (%dx%d)" % [
						tex_name + ext, grass_tex.get_width(), grass_tex.get_height()
					])
					break
		
		if not grass_tex:
			print("  [Warning] Missing texture for Grass Type %d (%s), skipping." % [
				grass_type, tex_name
			])
			continue

		# Prepare MultiMesh
		var multi_mesh = MultiMesh.new()
		multi_mesh.transform_format = MultiMesh.TRANSFORM_3D
		multi_mesh.use_colors = true 
		multi_mesh.instance_count = positions.size()
		multi_mesh.mesh = grass_primitive_mesh
		
		multi_mesh.mesh.custom_aabb = AABB(Vector3(-1, 0, -1), Vector3(2, 2, 2))
		
		# Set instances
		for i in range(positions.size()):
			var tile_pos = positions[i]
			var x = tile_pos.x
			var y = tile_pos.y
			
			var idx = y * TERRAIN_SIZE + x
			var h_base = heightmap[idx] if idx < heightmap.size() else 0.0
			
			# Coordinate Fix: Matches Terrain Mesh mapping
			# Godot X = MU Y (y)
			# Godot Z = MU X (x) with Z-flip offset (x - 255)
			# Height 'h' comes from index [y, x] which is correct for Data(y, x)
			
			var gx = float(y)
			var gz = float(x) - (TERRAIN_SIZE - 1.0)
			
			# Position
			# Aligned with New Terrain & Objects:
			# Godot X = MU Row (y)
			# Godot Z = MU Col (x) - 255
			var pos_vec = Vector3(float(y), h_base, float(x) - (TERRAIN_SIZE - 1.0)) 
			
			# Rotation: Identity (Sven quads are world-aligned)
			var t = Transform3D(Basis(), pos_vec)
			multi_mesh.set_instance_transform(i, t)
			
			# Logic for Atlas Offset:
			# SVEN: su = xf * 0.25 + rand % 4 * 0.25
			var xf = float(x)
			var base_variation = float(randi() % 4) * 0.25
			# Total variation offset passed to shader
			var total_variation = fmod(xf * 0.25 + base_variation, 1.0)
			
			# Store Random Phase for Wind in Color.G [0..1]
			var wind_phase = randf()
			
			multi_mesh.set_instance_color(i, Color(total_variation, wind_phase, 0.0, 1.0))
			
		# Material
		var mat = ShaderMaterial.new()
		mat.shader = load("res://core/shaders/grass_billboard.gdshader")
		mat.set_shader_parameter("grass_texture", grass_tex)
		mat.set_shader_parameter("wind_strength", 0.6)
		mat.set_shader_parameter("wind_speed", 0.6)
		
		var mm_inst = MultiMeshInstance3D.new()
		mm_inst.multimesh = multi_mesh
		mm_inst.material_override = mat
		mm_inst.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(mm_inst)
		grass_mesh = mm_inst # Keep reference to last one (hacky but OK for now)

func _spawn_objects():
	if not object_data or object_data.is_empty():
		return
		
	print("[MUTerrain] Spawning %d objects..." % object_data.size())
	
	# 1. BMD path: Objects are in Data/ObjectX/
	var world_dir = "Object" + str(world_id)
	var model_dir = data_path.path_join(world_dir)
	var spawned_count = 0
	var missing_types = []
	
	for obj in object_data:
		var type = obj.type
		var bmd_name = ""
		
		# Find the base type in the map
		var base_type = -1
		if LORENCIA_OBJECT_MAP.has(type):
			base_type = type
		else:
			# Check for ranged types (e.g., Tree01-Tree13 starts at 0)
			# Find the highest base_type <= type
			var keys = LORENCIA_OBJECT_MAP.keys()
			keys.sort()
			for k in keys:
				if k <= type:
					base_type = k
				else:
					break
		
		if base_type != -1:
			var pattern = LORENCIA_OBJECT_MAP[base_type]
			if "%02d" in pattern:
				# Calculate index relative to base
				var idx = (type - base_type) + 1
				bmd_name = pattern % idx
			else:
				bmd_name = pattern
		
		if bmd_name.is_empty():
			if not type in missing_types:
				missing_types.append(type)
			continue
			
		var bmd_path = model_dir.path_join(bmd_name + ".bmd")
		var abs_path = ProjectSettings.globalize_path(bmd_path)
		
		if not FileAccess.file_exists(abs_path):
			if spawned_count == 0: # Only log first few missing ones to avoid spam
				print("  [MUTerrain] Missing BMD at: %s" % bmd_path)
			continue
			
		# Parse and build the mesh
		var parser = BMDParser.new()
		if parser.parse_file(abs_path):
			# Create a node for the object
			var parent_node = Node3D.new()
			parent_node.name = bmd_name + "_" + str(spawned_count)
			add_child(parent_node)
			
			# Position and Rotation: Already converted to Transpose space in Parser
			parent_node.position = obj.position
			parent_node.quaternion = obj.rotation
			
			# MU objects are often scaled (SVEN/MU scale factor)
			parent_node.scale = Vector3.ONE * obj.scale
			
			# Add meshes to the newly parent_node directly
			var static_body = StaticBody3D.new()
			static_body.name = "PickingBody"
			static_body.set_meta("bmd_path", abs_path)
			static_body.set_meta("mu_euler", obj.mu_euler)
			static_body.set_meta("mu_pos", obj.mu_pos_raw)
			static_body.set_meta("mu_scale", obj.scale)
			parent_node.add_child(static_body)
			
			for mesh_idx in range(parser.get_mesh_count()):
				var bmd_mesh = parser.get_mesh(mesh_idx)
				var mesh_instance = MUMeshBuilder.create_mesh_instance(
					bmd_mesh, null, abs_path, parser, true
				)
				if mesh_instance:
					parent_node.add_child(mesh_instance)
					
					# SVEN compatibility: HiddenMesh == -2 means invisible but with collision
					if obj.hidden_mesh == -2:
						mesh_instance.visible = false
					
					
					# Add collision shape for picking
					if mesh_instance.mesh:
						var shape = CollisionShape3D.new()
						shape.shape = mesh_instance.mesh.create_convex_shape()
						static_body.add_child(shape)
			
			# Spawn Fire Effects (Once per object)
			_check_for_fire_effect(parent_node, obj.type)
			
			spawned_count += 1
		else:
			push_error("[MUTerrain] Failed to parse BMD: " + bmd_path)
			
	print("[MUTerrain] Spawned %d objects successfully." % spawned_count)
	if not missing_types.is_empty():
		missing_types.sort()
		print("[MUTerrain] Unknown object types: ", missing_types)

func is_walkable(tile_x: int, tile_y: int) -> bool:
	# Check if a tile is walkable based on attributes
	if attributes.is_empty():
		return true  # No attributes loaded, assume walkable
	
	if tile_x < 0 or tile_x >= TERRAIN_SIZE or tile_y < 0 or tile_y >= TERRAIN_SIZE:
		return false  # Out of bounds
	
	var idx = tile_y * TERRAIN_SIZE + tile_x
	var attr = attributes[idx]
	
	# Attribute 0 = walkable, 1 = blocked, 2+ = special zones
	return attr == 0 or attr == 2  # Walkable or safe zone

func get_attribute(tile_x: int, tile_y: int) -> int:
	# Get attribute value for a tile
	if attributes.is_empty():
		return 0
	
	if tile_x < 0 or tile_x >= TERRAIN_SIZE or tile_y < 0 or tile_y >= TERRAIN_SIZE:
		return 1  # Out of bounds = blocked
	
	var idx = tile_y * TERRAIN_SIZE + tile_x
	return attributes[idx]

func get_height_at_world(world_pos: Vector3) -> float:
	# Verified Mode 4 (Transpose) Mapping:
	# Godot X -> MU Row (y)
	# Godot Z -> MU Col (x) - Offset
	# MU Col (x) = Godot Z + (TERRAIN_SIZE - 1.0)
	
	var mu_y = int(world_pos.x)
	var mu_x = int(world_pos.z + (TERRAIN_SIZE - 1.0))
	
	if mu_x < 0 or mu_x >= TERRAIN_SIZE or mu_y < 0 or mu_y >= TERRAIN_SIZE:
		return 0.0
		
	var idx = mu_y * TERRAIN_SIZE + mu_x
	if idx < heightmap.size():
		return heightmap[idx]
	return 0.0

func _check_for_fire_effect(parent: Node3D, type: int):
	var fires = [] # Array of {offset: Vector3, type: int}
	
	# Coordinate Mapping: MU(x,y,z) -> Godot(y,z,x) * 0.01 (See coordinate_utils.gd)
	match type:
		50: # FireLight01 (Torch)
			# SVEN: (0, 0, 200) -> Godot (0, 2.0, 0)
			fires.append({offset=Vector3(0, 2.0, 0), type=0})
			
		51: # FireLight02 (Wall Torch)
			# SVEN: (0, -30, 60) -> Godot (-0.3, 0.6, 0)
			fires.append({offset=Vector3(-0.3, 0.6, 0), type=0})
			
		52: # Bonfire01
			# SVEN: (0, 0, 60) -> Godot (0, 0.6, 0)
			fires.append({offset=Vector3(0, 0.6, 0), type=1}) # fire02.png
			
		130: # Light01 (Fire without mesh)
			# SVEN: (0, 0, 0)
			fires.append({offset=Vector3.ZERO, type=0})
			_hide_meshes(parent)
			
		131, 132: # Light02/03
			_hide_meshes(parent)
		
		# Removed: 90 (StreetLight), 150 (Candle)
		
		80: # Bridge01
			# SVEN: (90, -200, 30) -> Godot (-2.0, 0.3, 0.9)
			# SVEN: (90, 200, 30)  -> Godot (2.0, 0.3, 0.9)
			fires.append({offset=Vector3(-2.0, 0.3, 0.9), type=0})
			fires.append({offset=Vector3(2.0, 0.3, 0.9), type=0})
			
		55: # DungeonGate01
			# SVEN: (-150, -150, 140) -> Godot (-1.5, 1.4, -1.5)
			# SVEN: (150, -150, 140)  -> Godot (-1.5, 1.4, 1.5)
			fires.append({offset=Vector3(-1.5, 1.4, -1.5), type=0})
			fires.append({offset=Vector3(-1.5, 1.4, 1.5), type=0})
			
	# Spawn all fires
	if MUFireScript and not fires.is_empty():
		for f in fires:
			# Apply offset relative to object rotation
			var fire_pos = parent.global_transform.origin + parent.global_transform.basis * f.offset
			MUFireScript.create(parent, fire_pos, f.type)

func _hide_meshes(parent: Node3D):
	for child in parent.get_children():
		if child is MeshInstance3D:
			child.visible = false


func _find_case_insensitive_path(path: String) -> String:
	var global_path = ProjectSettings.globalize_path(path)
	if FileAccess.file_exists(global_path):
		return global_path
	
	# Attempt case recovery
	var dir_path = global_path.get_base_dir()
	var target_file = global_path.get_file().to_lower()
	
	if not DirAccess.dir_exists_absolute(dir_path):
		return ""
		
	var dir = DirAccess.open(dir_path)
	if dir:
		dir.list_dir_begin()
		var fn = dir.get_next()
		while fn != "":
			if fn.to_lower() == target_file:
				return dir_path.path_join(fn)
			fn = dir.get_next()
	return ""
