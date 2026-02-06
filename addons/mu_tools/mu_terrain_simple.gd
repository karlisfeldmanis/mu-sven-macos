extends Node3D

class_name MUTerrainSimple

const TERRAIN_SIZE = 256
# Object Map (Restored from mu_terrain.gd for reference, though we might load it dynamically or keep it here for simplicity)
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
@export var data_path: String = "res://reference/MuMain/src/bin/Data"

const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUTerrainMeshBuilder = preload("res://addons/mu_tools/mu_terrain_mesh_builder.gd")
const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
const MUFileUtil = preload("res://addons/mu_tools/mu_file_util.gd")
const MUBMDRegistry = preload("res://addons/mu_tools/mu_bmd_registry.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/mesh_builder.gd")

var heightmap: PackedFloat32Array
var map_data: MUTerrainParser.MapData
var object_data: Array
var attributes: PackedByteArray
var lightmap: Image

func _ready():
	load_world()
	# _setup_environment() # Disabled: Let MainSimple handle lighting/environment

func _process(_delta):
	_update_uv_scrolling()
	_update_transparency()

func _update_transparency():
	# Sven: if (HeroTile == 4) o->AlphaTarget = 0.f; for House Walls 125/126.
	# We need the character position. 
	var character = get_tree().get_first_node_in_group("player_character")
	if not character or attributes.is_empty(): return
	
	var char_pos = character.global_position
	# Convert Godot Pos to Terrain Grid
	# Mirror-X Mapping:
	var tx = int(255.0 - char_pos.x) 
	var tz = int(char_pos.z)
	
	if tx < 0 or tx >= 256 or tz < 0 or tz >= 256: return
	
	var attr_idx = tz * 256 + tx
	var tile_attr = attributes[attr_idx]
	
	# MU Lorencia building index is usually 4 in several versions
	var is_inside = (tile_attr == 4)
	
	for node in get_tree().get_nodes_in_group("house_walls_transparency"):
		if node is MeshInstance3D:
			node.visible = !is_inside

func _update_uv_scrolling():
	# Sven: o->BlendMeshTexCoordV = -(int)WorldTime % 1000 * 0.001f;
	# 1.0 unit per second in MU world (1000ms).
	var scroll_v = -fmod(Time.get_ticks_msec(), 1000) * 0.001
	
	for node in get_tree().get_nodes_in_group("uv_scrolling_objects"):
		if node is MeshInstance3D:
			# In MU, this usually applies to a specific "BlendMesh" layer.
			# For Godot, we'll try to apply it to all surfaces for now.
			for i in range(node.get_surface_override_material_count()):
				var mat = node.get_surface_override_material(i)
				if not mat:
					mat = node.mesh.surface_get_material(i)
					if mat:
						mat = mat.duplicate() # Make unique if not already
						node.set_surface_override_material(i, mat)
				
				if mat and (mat is ORMMaterial3D or mat is StandardMaterial3D):
					mat.uv1_offset.y = scroll_v

func _setup_environment():
	print("[MUTerrainSimple] Applying Environment Settings (Sven Logic)...")
	
	# 1. Directional Light (Sven: 0.5, -0.5, 0.5)
	# Vector(0.5, -0.5, 0.5) pointing towards origin? Or Light direction vector?
	# In OpenGL, Light Position usually implies direction if w=0. 
	# Sven uses `DotProduct(Normal, Light)` so it's a direction vector.
	# Normalized: (0.577, -0.577, 0.577). 
	# Looking at the vector (x, y, z) -> Right, Down, Back (OpenGL). Godot Y is Up, Z is Forward/Back.
	# Sven Z is Up. Godot Y is Up. 
	# Sven: X=Right, Y=Down(?!), Z=Up? Need to recall Sven Coordinate System.
	# Sven ZzzOpenglUtil: `Vector(0.5f, -0.5f, 0.5f, Light);`
	# If Sven Z is Up: Light is coming from (+X, -Y, +Z)? -Y is usually "Forward" or "Down"?
	# In standard OpenGL, -Z is forward. 
	# Let's assume the vector is a Direction TO the light or FROM the light?
	# `DotProduct(TerrainNormal, Light)`. TerrainNormal is Up. If Light is (0,0,1) (Up), Dot is 1. 
	# So Light vector matches Normal direction. Thus it's the direction TO the light source.
	# Vector(0.5, -0.5, 0.5) = Right, Back?, Up.
	# Godot DirectionalLight3D points -Z (Forward). We need to rotate it.
	
	var light_node = get_node_or_null("DirectionalLight3D")
	if not light_node:
		light_node = DirectionalLight3D.new()
		light_node.name = "DirectionalLight3D"
		add_child(light_node)
		light_node.owner = self.owner # Persist if saved
		
	# Sven Vector: (0.5, -0.5, 0.5) (Direction TO Light Source)
	# Godot Coordinate conversion (Sven -> Godot):
	# Sven X -> Godot X
	# Sven Y -> Godot -Z
	# Sven Z -> Godot Y
	var godot_sun_vec = Vector3(0.5, 0.5, 0.5) # Converted from (0.5, -(-0.5), 0.5)
	
	# DirectionalLight3D rays travel in -Z. 
	# To make rays come FROM godot_sun_vec, we point -Z at -godot_sun_vec.
	light_node.look_at(-godot_sun_vec.normalized(), Vector3.UP) 
	light_node.light_energy = 1.0
	light_node.shadow_enabled = true
	
	# 2. Fog & Background
	# Sven Default: FogColor = { 30/256.0, 20/256.0, 10/256.0 }
	var fog_color = Color(30/256.0, 20/256.0, 10/256.0)
	
	var env: Environment = null
	var world_env = get_tree().root.find_child("WorldEnvironment", true, false)
	if world_env:
		env = world_env.environment
	
	if not env:
		var cam = get_viewport().get_camera_3d()
		if cam: env = cam.environment

	if env:
		env.background_mode = Environment.BG_COLOR
		env.background_color = fog_color
		env.fog_enabled = true
		env.fog_mode = Environment.FOG_MODE_EXPONENTIAL
		env.fog_light_color = fog_color
		# Sven Density 0.0004. Godot scale is smaller (meters vs cm).
		# If 1 unit = 1 meter, and Sven 1 unit = 1 cm?
		# Sven coordinates are huge (10000+). Godot is scaled 0.01.
		# If we scaled world by 0.01, density should be x100?
		# 0.0004 * 100 = 0.04.
		env.fog_density = 0.04 
		print("[MUTerrainSimple] Environment Set: Fog Density 0.04, Color ", fog_color)


func load_world():
	print("[MUTerrainSimple] Loading World %d from %s..." % [world_id, data_path])
	
	var parser = MUTerrainParser.new()
	var mesh_builder = MUTerrainMeshBuilder.new()
	
	var world_dir = "World" + str(world_id + 1)
	var base_data_path = data_path
	if data_path.get_file() == world_dir:
		base_data_path = data_path.get_base_dir()
		
	var world_full_path = base_data_path.path_join(world_dir)
	var height_file = world_full_path.path_join("TerrainHeight.OZB")
	var mapping_file = world_full_path.path_join("EncTerrain" + str(world_id + 1) + ".map")
	var objects_file = world_full_path.path_join("EncTerrain" + str(world_id + 1) + ".obj")
	var att_file = world_full_path.path_join("EncTerrain" + str(world_id + 1) + ".att")
	var light_file = world_full_path.path_join("TerrainLight.OZJ")

	# 1. Load Data
	heightmap = parser.parse_height_file(height_file)
	
	# Mapping file fallback logic
	var is_encrypted = true
	if not FileAccess.file_exists(mapping_file):
		# Try "Terrain.map" (Unencrypted/Simple format)
		var fb_mapping = world_full_path.path_join("Terrain.map")
		if FileAccess.file_exists(fb_mapping):
			mapping_file = fb_mapping
			is_encrypted = false
			print("[MUTerrainSimple] EncTerrain.map not found, falling back to: ", mapping_file.get_file())
	
	map_data = parser.parse_mapping_file(mapping_file, is_encrypted)
	object_data = parser.parse_objects_file(objects_file)
	attributes = parser.parse_attributes_file(att_file)

	# 2. Load Lightmap
	var light_tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(light_file))
	if light_tex:
		lightmap = light_tex.get_image()
		# NOTE: flip_y() removed to test alignment. 
		# If layers are flipped in parser, we need to decide if Lightmap follows.
		# lightmap.flip_y() 

	# 3. Create Single Terrain Mesh
	var mesh = mesh_builder.build_terrain_array_mesh(heightmap, lightmap)
	var terrain_mi = MeshInstance3D.new()
	terrain_mi.mesh = mesh
	
	# 5. Create Water
	_create_water_mesh()
	
	# 6. Create Grass
	_create_grass_mesh_simple()

	terrain_mi.name = "TerrainMesh"
	add_child(terrain_mi)
	
	# Enable Collision for Raycasting
	terrain_mi.create_trimesh_collision()
	
	# 4. Setup Materials
	_setup_material(terrain_mi)
	
	# 5. Spawn Objects
	_spawn_objects()

	print("[MUTerrainSimple] World loaded successfully.")

func _setup_material(mesh_instance: MeshInstance3D):
	# ... (Copied logic for material setup, simplified if possible)
	# For simplicity, I'll copy the core material setup logic from mu_terrain.gd
	# but avoid the complex batching/texture array logic if possible? 
	# Actually, the shader NEEDS the texture array, so we must build it.
	
	print("[MUTerrainSimple] Building Texture Array...")
	var texture_map = {}
	var base_texture_names = [
		"TileGrass01", "TileGrass02", "TileGround01", "TileGround02", "TileGround03",
		"TileWater01", "TileWood01", "TileRock01", "TileRock02", "TileRock03",
		"TileRock04", "TileRock05", "TileRock06", "TileRock07"
	]
	
	# Load base logic similar to mu_terrain.gd
	for i in range(base_texture_names.size()):
		var img = _load_tile_image(base_texture_names[i])
		if img: texture_map[i] = img
		
	# Load ExtTiles
	for i in range(1, 243):
		var img = _load_tile_image("ExtTile%02d" % i)
		if img:
			var target_idx = 13 + i
			if target_idx < 256: texture_map[target_idx] = img

	# Build Array
	var final_images: Array[Image] = []
	var max_size = 256
	var fallback_img = Image.create(max_size, max_size, false, Image.FORMAT_RGB8)
	fallback_img.fill(Color.BLACK)
	
	for i in range(256):
		var img = texture_map.get(i)
		# Ensure resize if needed
		if img and (img.get_width() != max_size or img.get_height() != max_size):
			img.resize(max_size, max_size, Image.INTERPOLATE_LANCZOS)
		
		final_images.append(img if img else fallback_img)
		
	var tex_array = Texture2DArray.new()
	tex_array.create_from_images(final_images)
	
	# Scale LUT removed (Reverted to High-Res 1.0 Scale)
	
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/mu_terrain.gdshader")
	mat.set_shader_parameter("tile_textures", tex_array)

	
	# Maps
	if map_data:
		var l1 = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		var l2 = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		var alpha = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
		
		for y in range(TERRAIN_SIZE):
			for x in range(TERRAIN_SIZE):
				var idx = y * TERRAIN_SIZE + x
				# No mirroring here because UVs follow the array indices (cx, cy)
				l1.set_pixel(x, y, Color(map_data.layer1[idx] / 255.0, 0, 0))
				l2.set_pixel(x, y, Color(map_data.layer2[idx] / 255.0, 0, 0))
				alpha.set_pixel(x, y, Color(map_data.alpha[idx], 0, 0))
				
		mat.set_shader_parameter("layer1_map", ImageTexture.create_from_image(l1))
		mat.set_shader_parameter("layer2_map", ImageTexture.create_from_image(l2))
		mat.set_shader_parameter("alpha_map", ImageTexture.create_from_image(alpha))
		
	# Noise + Grass Offset (Simplified default)
	var noise = FastNoiseLite.new()
	mat.set_shader_parameter("noise_texture", ImageTexture.create_from_image(noise.get_image(256, 256)))
	
	# Grass Offset
	var offset_img = Image.create(TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8)
	# Fill random
	for i in range(TERRAIN_SIZE):
		for j in range(TERRAIN_SIZE):
			offset_img.set_pixel(i, j, Color(randf(), 0, 0))
	mat.set_shader_parameter("grass_offset_map", ImageTexture.create_from_image(offset_img))

	mesh_instance.material_override = mat

func _load_tile_image(name: String) -> Image:
	# Simplified loader, reuse mu_terrain logic
	var extensions = [".OZJ", ".ozj", ".OZT", ".ozt", ".jpg", ".JPG", ".tga"]
	var world_dir = "World" + str(world_id + 1)
	var base = data_path
	if data_path.get_file() == world_dir: base = data_path.get_base_dir()
	
	for ext in extensions:
		# Check specific then global
		var paths = [
			base.path_join(world_dir).path_join(name + ext),
			base.path_join(name + ext)
		]
		for p in paths:
			if FileAccess.file_exists(p):
				var tex = MUTextureLoader.load_mu_texture(p)
				if tex: return tex.get_image()
	return null

func _spawn_objects():
	print("[MUTerrainSimple] Spawning objects...")
	var world_dir = "Object" + str(world_id + 1)
	var model_dir = data_path.path_join(world_dir)
	if data_path.get_file() == ("World"+str(world_id)): # Fix path if needed
		model_dir = data_path.get_base_dir().path_join(world_dir)
		
	var spawned_count = 0
	
	for obj in object_data:
		var bmd_name = _get_bmd_name(obj.type)
		if bmd_name.is_empty(): continue
		
		var bmd_path = model_dir.path_join(bmd_name + ".bmd")
		var abs_path = MUFileUtil.resolve_case(bmd_path)
		
		if not MUFileUtil.file_exists(abs_path): continue
		
		var parser = MUBMDRegistry.get_bmd(abs_path)
		if parser:
			var parent_node = Node3D.new()
			parent_node.name = bmd_name + "_" + str(spawned_count)
			add_child(parent_node)
			
			# Mirror-X Mapping via Reflection:
			# Godot X = 255 - MU Column / 100
			# Mirror-X Mapping via Reflection:
			# Godot X = 255 - MU Column / 100
			# Mirror-X Mapping via Reflection:
			# Godot X = 255 - MU Column / 100
			# Universal Math: Restoring Stable Baseline (Scale X = -1 + 180 Rot).
			# This ensures visibility. We will debug Chirality separately.
			
			parent_node.position.x = 255.0 - (obj.mu_pos_raw.x / 100.0)
			parent_node.position.y = obj.mu_pos_raw.z / 100.0
			parent_node.position.z = obj.mu_pos_raw.y / 100.0
			
			# MU objects typically use static scaling
			# Apply Mirror-X reflection at the scale level
			parent_node.transform.basis = Basis(obj.rotation)
			parent_node.scale = Vector3(-1.0, 1.0, 1.0) * obj.scale
			
			for mesh_idx in range(parser.get_mesh_count()):
				var bmd_mesh = parser.get_mesh(mesh_idx)
				var mesh_instance = MUMeshBuilder.create_mesh_instance(
					bmd_mesh, null, abs_path, parser, true
				)
				if mesh_instance:
					# Verify Shadow Casting is ON
					mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
					parent_node.add_child(mesh_instance)
					var type = obj.type
					# Sven Logic: HiddenMesh == -2 hides the model (replaced by effects)
					# Objects 130-132 (Lights), 90 (Streetlight), 52 (Bonfire), 150 (Candle)
					if obj.hidden_mesh == -2 or type == 130 or type == 131 or type == 132 or type == 90 or type == 52 or type == 150:
						mesh_instance.visible = false
						mesh_instance.add_to_group("hidden_light_sources")
						
						# Spawn Godot OmniLight3D (only once per parent_node)
						if mesh_idx == 0:
							var light = OmniLight3D.new()
							parent_node.add_child(light)
							light.position = mesh_instance.position
							
							match type:
								90: # Streetlight
									light.light_color = Color(0.65, 0.52, 0.39)
									light.light_energy = 2.0
									light.omni_range = 10.0
									light.position.y += 3.0
								150: # Candle
									light.light_color = Color(0.45, 0.27, 0.09)
									light.light_energy = 0.8
									light.omni_range = 3.0
									light.position.y += 0.5
								52: # Bonfire
									light.light_color = Color(0.8, 0.4, 0.1)
									light.light_energy = 3.0
									light.omni_range = 15.0
									light.position.y += 0.5
								130, 131, 132: # Fire Lights
									light.light_color = Color(0.7, 0.3, 0.1)
									light.light_energy = 1.5
									light.omni_range = 8.0
							
							light.shadow_enabled = true
					
					# Sven Logic: House Walls (125, 126) transparency
					if type == 121 + 4 or type == 121 + 5:
						mesh_instance.add_to_group("house_walls_transparency")
					
					# Sven Logic: UV Scrolling for Houses (118, 119)
					if type == 115 + 3 or type == 115 + 4:
						mesh_instance.add_to_group("uv_scrolling_objects")
						
			spawned_count += 1
			
	print("[MUTerrainSimple] Spawned %d objects." % spawned_count)

func _get_bmd_name(type: int) -> String:
	if LORENCIA_OBJECT_MAP.has(type): 
		var val = LORENCIA_OBJECT_MAP[type]
		if "%02d" in val: return val % 1 # Base of range is always Index 1
		return val
		
	var keys = LORENCIA_OBJECT_MAP.keys()
	keys.sort()
	var base = -1
	for k in keys:
		if k <= type: base = k
		else: break
	
	if base != -1:
		var pat = LORENCIA_OBJECT_MAP[base]
		if "%02d" in pat: return pat % ((type - base) + 1)
		return pat
	return ""

func _create_water_mesh():
	if not map_data or map_data.water_tiles.is_empty(): return
	print("[MUTerrainSimple] Creating water mesh (%d tiles)..." % map_data.water_tiles.size())
	
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	for tile in map_data.water_tiles:
		var x = tile.x
		var y = tile.y
		var idx = y * TERRAIN_SIZE + x
		var h_raw = heightmap[idx] if idx < heightmap.size() else 0.0
		var h = (h_raw * 1.5) / 100.0
		h += 0.15
		
		# Mirror-X Mapping:
		var gx = 255.0 - float(x)
		var gx_prev = 255.0 - (float(x) + 1.0)
		var gz = float(y)
		var gz_next = float(y) + 1.0
		
		var v0 = Vector3(gx, h, gz)
		var v1 = Vector3(gx_prev, h, gz)
		var v2 = Vector3(gx_prev, h, gz_next)
		var v3 = Vector3(gx, h, gz_next)
		
		# Quad (v0, v2, v1) and (v0, v3, v2)
		st.set_uv(Vector2(0,0)); st.add_vertex(v0)
		st.set_uv(Vector2(1,1)); st.add_vertex(v2)
		st.set_uv(Vector2(1,0)); st.add_vertex(v1)
		
		st.set_uv(Vector2(0,0)); st.add_vertex(v0)
		st.set_uv(Vector2(0,1)); st.add_vertex(v3)
		st.set_uv(Vector2(1,1)); st.add_vertex(v2)
		
	var mesh = st.commit()
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/water_shader.gdshader")
	# Load water texture (simplified)
	var water_tex_path = data_path.path_join("World" + str(world_id + 1)).path_join("TileWater01.OZJ")
	if not FileAccess.file_exists(water_tex_path): # Try OZT or root
		water_tex_path = data_path.path_join("TileWater01.OZJ")
		
	var tex = MUTextureLoader.load_mu_texture(water_tex_path)
	if tex: mat.set_shader_parameter("water_texture", tex)
	
	var mi = MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	add_child(mi)

func _create_grass_mesh_simple():
	if not map_data or map_data.grass_tiles.is_empty(): return
	print("[MUTerrainSimple] Creating grass...")
	
	# Primitive Mesh (Diagonal Quad)
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var h = 0.6; var w = 0.25
	var dl_b = Vector3(-w, 0, w); var dr_b = Vector3(w, 0, -w)
	var dl_t = Vector3(-w, h, w); var dr_t = Vector3(w, h, -w)
	
	st.set_color(Color.WHITE); st.set_uv(Vector2(0,0)); st.add_vertex(dl_t)
	st.set_color(Color(0.6,0.6,0.6)); st.set_uv(Vector2(1,1)); st.add_vertex(dr_b)
	st.set_color(Color.WHITE); st.set_uv(Vector2(1,0)); st.add_vertex(dr_t)
	
	st.set_color(Color.WHITE); st.set_uv(Vector2(0,0)); st.add_vertex(dl_t)
	st.set_color(Color(0.6,0.6,0.6)); st.set_uv(Vector2(0,1)); st.add_vertex(dl_b)
	st.set_color(Color(0.6,0.6,0.6)); st.set_uv(Vector2(1,1)); st.add_vertex(dr_b)
	
	var grass_mesh = st.commit()
	
	# Create MultiMesh per type
	for type in map_data.grass_tiles.keys():
		var positions = map_data.grass_tiles[type]
		var mm = MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.use_colors = true
		mm.instance_count = positions.size() * 8
		mm.mesh = grass_mesh
		
		var idx_count = 0
		for pos in positions:
			# Skip if masked (Attribute check skipped for simplicity or add back if critical)
			var x = pos.x; var y = pos.y
			var tidx = int(y) * TERRAIN_SIZE + int(x)
			if tidx < attributes.size():
				var attr = attributes[tidx]
				if (attr & 0x04) != 0: continue
			
			# Corrected Height Scaling: (Byte * 1.5 factor) / 100.0 scale
			var h_raw = heightmap[tidx] if tidx < heightmap.size() else 0.0
			var h_base = (h_raw * 1.5) / 100.0
			
			for k in range(8):
				var sub_x = 0.5 + randf_range(-0.4, 0.4)
				var sub_y = 0.5 + randf_range(-0.4, 0.4) # logic approx
				# Mirror-X Mapping:
				var gpos = Vector3(
					255.0 - (float(x) + sub_x), 
					h_base, 
					float(y) + sub_y
				)
				var t = Transform3D().scaled(Vector3.ONE * (0.6 + randf()*0.6))
				t.origin = gpos
				mm.set_instance_transform(idx_count, t)
				mm.set_instance_color(idx_count, Color(randf(), randf(), 0, 1))
				idx_count += 1
		
		# Shrink to fit actual count
		mm.instance_count = idx_count
		
		var mi = MultiMeshInstance3D.new()
		mi.multimesh = mm
		
		var mat = ShaderMaterial.new()
		mat.shader = load("res://core/shaders/grass_billboard.gdshader")
		var tex = _load_grass_texture("TileGrass%02d" % (type + 1))
		if tex: mat.set_shader_parameter("grass_texture", tex)
		
		mi.material_override = mat
		add_child(mi)

func _load_grass_texture(name):
	var extensions = [".tga", ".TGA", ".ozj", ".OZJ"]
	var world_dir = "World" + str(world_id + 1)
	var p = data_path.path_join(world_dir)
	for ext in extensions:
		var f = p.path_join(name + ext)
		if FileAccess.file_exists(MUFileUtil.resolve_case(f)):
			return MUTextureLoader.load_mu_texture(f)
	return null

func _spawn_objects_placeholder():
	pass

