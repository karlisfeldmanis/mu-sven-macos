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
const MUFileUtil = preload("res://addons/mu_tools/mu_file_util.gd")
const MUObjectEffectManager = preload("res://addons/mu_tools/mu_object_effect_manager.gd")

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
	terrain_mesh.name = "TerrainMesh"
	terrain_mesh.mesh = mesh
	print("[MUTerrain] Created terrain mesh with %d surfaces" % 
			terrain_mesh.mesh.get_surface_count())
	add_child(terrain_mesh)
	
	# 6. Setup Material
	_setup_material()
	

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
		var abs_world_path = MUFileUtil.resolve_case(world_path)
		if MUFileUtil.file_exists(abs_world_path):
			var direct_tex = MUTextureLoader.load_mu_texture(abs_world_path)
			if direct_tex:
				return direct_tex.get_image()
			
		# 2. Try global Data folder (Data/ExtTileXX)
		var global_path = base_data_path.path_join(tex_name + ext)
		var abs_global_path = MUFileUtil.resolve_case(global_path)
		if MUFileUtil.file_exists(abs_global_path):
			var direct_tex = MUTextureLoader.load_mu_texture(abs_global_path)
			if direct_tex:
				return direct_tex.get_image()
			
	# If we reach here, no file was found. 
	# For ExtTiles, this is expected if they don't exist, so we don't warn unless it's a base tile.
	if not tex_name.begins_with("ExtTile"):
		push_warning("[MUTerrain] ALL attempts failed to load tile: " + tex_name)
	return null




func _create_grass_mesh():
	if not map_data or map_data.grass_tiles.is_empty():
		return
	
	# 1:1 Sven Diagonal Quad Geometry
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	var h = 0.6 # Taller to match reference (approx 0.6m)
	# Single Diagonal Quad (SVEN Style: Front-Left to Back-Right)
	var w = 0.25 # Slightly wider for single blade coverage
	
	# Diagonal: (-w, 0, w) to (w, 0, -w) relative to center
	# This creates a South-East facing diagonal (Top-Left to Bottom-Right in Map View)
	var dl_b = Vector3(-w, 0.0, w)   # Bottom Left (Front-Left)
	var dr_b = Vector3(w, 0.0, -w)   # Bottom Right (Back-Right)
	var dl_t = Vector3(-w, h, w)	 # Top Left
	var dr_t = Vector3(w, h, -w)	 # Top Right
	
	# Vertices with colors for Ambient Occlusion (dark roots)
	var c_t = Color(1, 1, 1, 1) # Top
	var c_b = Color(0.6, 0.6, 0.6, 1) # Bottom (Soft shadow, not pitch black)
	
	# Quad generation (Counter-Clockwise)
	# Triangle 1: Top-Left, Bottom-Right, Top-Right
	st.set_color(c_t); st.set_uv(Vector2(0, 0)); st.add_vertex(dl_t)
	st.set_color(c_b); st.set_uv(Vector2(1, 1)); st.add_vertex(dr_b)
	st.set_color(c_t); st.set_uv(Vector2(1, 0)); st.add_vertex(dr_t)
	
	# Triangle 2: Top-Left, Bottom-Left, Bottom-Right
	st.set_color(c_t); st.set_uv(Vector2(0, 0)); st.add_vertex(dl_t)
	st.set_color(c_b); st.set_uv(Vector2(0, 1)); st.add_vertex(dl_b)
	st.set_color(c_b); st.set_uv(Vector2(1, 1)); st.add_vertex(dr_b)
		
	var grass_primitive_mesh = st.commit()
	
	print("[MUTerrain] Grass Generation Start: %d types found" % map_data.grass_tiles.size())
	
	# Pre-calculate row randomization for SVEN authenticity
	var row_random = []
	for r in range(TERRAIN_SIZE):
		row_random.append(float(randi() % 4) * 0.25)
	
	for grass_type in map_data.grass_tiles.keys():
		var raw_positions = map_data.grass_tiles[grass_type]
		if raw_positions.is_empty():
			continue
			
		# Attribute Masking: Filter out tiles that are NoMove, NoGround, or Water
		var positions = []
		for pos in raw_positions:
			var idx = int(pos.y) * TERRAIN_SIZE + int(pos.x)
			if idx < attributes.size():
				var attr = attributes[idx]
				# TW_NOMOVE (0x04) | TW_NOGROUND (0x08) | TW_WATER (0x10)
				# Grass should only grow on walkable ground
				if (attr & 0x04) != 0 or (attr & 0x08) != 0 or (attr & 0x10) != 0:
					continue
			positions.append(pos)

		if positions.is_empty():
			print("[MUTerrain]   Type %d: 0 tiles (after attribute masking)" % grass_type)
			continue
			
		print("[MUTerrain]   Type %d: %d tiles -> %d blades" % [
			grass_type, positions.size(), positions.size() * 8
		])
		
		var tex_name = "TileGrass%02d" % (grass_type + 1)
		var grass_tex: Texture2D = null
		
		# Texture Discovery
		var extensions = [".tga", ".TGA", ".ozj", ".OZJ", ".ozt", ".OZT", ".jpg"]
		var world_dir = "World" + str(world_id)
		var search_path = data_path.path_join(world_dir)
		
		for ext in extensions:
			var sub_path = world_dir.path_join(tex_name + ext)
			var path_check = search_path.path_join(tex_name + ext)
			var actual_path = MUFileUtil.resolve_case(path_check)
			if actual_path != "":
				var loaded = MUTextureLoader.load_mu_texture(actual_path)
				if loaded:
					grass_tex = loaded
					break
		
		if not grass_tex:
			continue

		var multi_mesh = MultiMesh.new()
		multi_mesh.transform_format = MultiMesh.TRANSFORM_3D
		multi_mesh.use_colors = true 
		multi_mesh.use_custom_data = true
		multi_mesh.instance_count = positions.size() * 8
		multi_mesh.mesh = grass_primitive_mesh
		
		for i in range(positions.size()):
			var tile_pos = positions[i]
			var x = tile_pos.x
			var y = tile_pos.y
			var idx = y * TERRAIN_SIZE + x
			var h_base = heightmap[idx] if idx < heightmap.size() else 0.0
			
			
			for sub in range(8):
				# Randomized scattering for 8 blades
				# Using a slightly structured but jittered 3x3-ish grid (skipping center)
				var grid_x = sub % 3
				var grid_y = sub / 3
				var sub_x = 0.15 + grid_x * 0.35 + randf_range(-0.1, 0.1)
				var sub_y = 0.15 + grid_y * 0.35 + randf_range(-0.1, 0.1)
				
				# Standard Row-Major Alignment: GodotX = x, GodotZ = y
				var pos_vec = Vector3(
					float(x) + sub_x, 
					h_base, 
					float(y) + sub_y
				)
				
				var random_scale = 0.6 + randf() * 0.6 # 0.6 to 1.2
				var random_rot = 0.0 # SVEN uses fixed rotation (world aligned)
				# But we apply a fixed diagonal in mesh, so no rotation needed here?
				# Wait, SVEN diagonal is fixed per tile. So all blades in tile parallel? YES.
				# Do we want all 8 blades parallel?
				# Authenticity: YES. Fields look like combed waves.
				
				var t = Transform3D()
				t = t.scaled_local(Vector3(random_scale, random_scale, random_scale))
				t.origin = pos_vec
				
				var inst_idx = i * 8 + sub
				multi_mesh.set_instance_transform(inst_idx, t)
				
				# SVEN Authentic UV Logic:
				# su = xf * Width + TerrainGrassTexture[yi]
				# xf = tile_x. Width = 0.25. Texture = row_random.
				var atlas_offset = (float(x) * 0.25) + row_random[int(y)]
				atlas_offset = fmod(atlas_offset, 1.0) # Wrap UV
				
				var individual_phase = fmod(
					float(x * 123.45 + y * 67.89 + sub * 13.5), 1.0)
				
				# COLOR: r=atlas (SVEN logic), g=sway, b=unused
				multi_mesh.set_instance_color(inst_idx, 
					Color(atlas_offset, individual_phase, 0.0, 1.0))
			
		var mat = ShaderMaterial.new()
		mat.shader = load("res://core/shaders/grass_billboard.gdshader")
		mat.set_shader_parameter("grass_texture", grass_tex)
		mat.set_shader_parameter("global_alpha", 0.8)
		
		# SVEN Lighting: Pass the full lightmap texture for bilinear sampling
		if lightmap:
			var light_tex = ImageTexture.create_from_image(lightmap)
			mat.set_shader_parameter("lightmap_texture", light_tex)
		
		var mm_inst = MultiMeshInstance3D.new()
		mm_inst.name = "GrassMesh"
		mm_inst.multimesh = multi_mesh
		mm_inst.material_override = mat
		mm_inst.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mm_inst.custom_aabb = AABB(Vector3(0, -5, -256), Vector3(256, 20, 256))
		add_child(mm_inst)

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
			print("Unknown Type: %d" % type)
			continue
			
		var bmd_path = model_dir.path_join(bmd_name + ".bmd")
		var abs_path = MUFileUtil.resolve_case(bmd_path)
		
		if not MUFileUtil.file_exists(abs_path):
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
			var initial_pos = obj.position
			parent_node.position = initial_pos
			
			if spawned_count < 15:
				var terrain_h = get_height_at_world(initial_pos)
				print("[Spawn] %s - MU_Z:%.1f Y:%.2f TerrH:%.2f Δ:%.2f" % [
					bmd_name, obj.mu_pos_raw.z, initial_pos.y, terrain_h, initial_pos.y - terrain_h
				])
			
			parent_node.quaternion = obj.rotation
			
			# MU objects are often scaled (SVEN/MU scale factor)
			parent_node.scale = Vector3.ONE * obj.scale
			
			# Remove heuristic height correction (replaced by constant offset)
			# _apply_height_correction(parent_node)
			
			# Add meshes to the newly parent_node directly
			var static_body = StaticBody3D.new()
			static_body.name = "PickingBody"
			static_body.set_meta("bmd_path", abs_path)
			static_body.set_meta("mu_euler", obj.mu_euler)
			# Metadata for object info
			static_body.set_meta("object_type", obj.type)
			static_body.set_meta("mu_pos", obj.mu_pos_raw)  # For diagnostics
			static_body.set_meta("mu_scale", obj.scale)
			parent_node.add_child(static_body)
			
			for mesh_idx in range(parser.get_mesh_count()):
				var bmd_mesh = parser.get_mesh(mesh_idx)
				var mesh_instance = MUMeshBuilder.create_mesh_instance(
					bmd_mesh, null, abs_path, parser, true, false, false, mesh_idx
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
			
			# Apply SVEN-style visual effects (Wind, Scaling, etc.)
			MUObjectEffectManager.apply_effects(parent_node, obj.type, world_id)
			
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
	# SVEN-compatible bilinear interpolation (RequestTerrainHeight)
	# Godot X → MU Row (y), Godot Z → MU Col (x) - Offset
	var mu_y = world_pos.x
	var mu_x = world_pos.z + (TERRAIN_SIZE - 1.0)
	
	# Bounds check
	if mu_x < 0 or mu_x >= TERRAIN_SIZE or mu_y < 0 or mu_y >= TERRAIN_SIZE:
		return 0.0
	
	# Get integer tile and decimal fraction (SVEN: xf/yf → xi/yi + xd/yd)
	var xi = int(mu_x)
	var yi = int(mu_y)
	var xd = mu_x - float(xi)
	var yd = mu_y - float(yi)
	
	# Clamp to valid sampling range
	xi = clampi(xi, 0, TERRAIN_SIZE - 2)
	yi = clampi(yi, 0, TERRAIN_SIZE - 2)
	
	# Sample 4 corners
	# CRITICAL: Heightmap is stored in COLUMN-MAJOR order: idx = x * SIZE + y
	# (not row-major yi * SIZE + xi like we initially thought)
	var idx1 = xi * TERRAIN_SIZE + yi           # (xi, yi)
	var idx2 = (xi + 1) * TERRAIN_SIZE + yi     # (xi+1, yi)  
	var idx3 = xi * TERRAIN_SIZE + (yi + 1)     # (xi, yi+1)
	var idx4 = (xi + 1) * TERRAIN_SIZE + (yi + 1)  # (xi+1, yi+1)
	
	if idx4 >= heightmap.size():
		# Fallback to simple lookup if out of bounds
		var idx = xi * TERRAIN_SIZE + yi
		return heightmap[idx] if idx < heightmap.size() else 0.0
	
	# Bilinear interpolation
	# idx1=(xi,yi) idx2=(xi+1,yi) idx3=(xi,yi+1) idx4=(xi+1,yi+1)
	var h1 = heightmap[idx1]
	var h2 = heightmap[idx2]
	var h3 = heightmap[idx3]
	var h4 = heightmap[idx4]
	
	# Interpolate along Y on left edge (xi), then right edge (xi+1)
	var left = h1 + (h3 - h1) * yd   # Between idx1 and idx3
	var right = h2 + (h4 - h2) * yd  # Between idx2 and idx4
	# Then interpolate along X
	return left + (right - left) * xd

func _apply_height_correction(node: Node3D):
	var pos = node.position
	var terrain_h = get_height_at_world(pos)
	var diff = pos.y - terrain_h
	
	# Heuristic: If object is significantly underground (> 2.0m),
	# assume the Z coordinate (Y in Godot) was intended as a relative offset.
	if diff < -2.0:
		# Check if the relative position would make sense (e.g. near 0 offset)
		var relative_params = terrain_h + pos.y
		
		# Only apply if it brings it strictly above ground or close to it
		node.position.y = relative_params
		
		# print("[MUTerrain] Corrected Height for %s: %.2f -> %.2f (Terrain: %.2f)" % [
		# 	node.name, pos.y, node.position.y, terrain_h
		# ])

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
	return MUFileUtil.resolve_case(path)
