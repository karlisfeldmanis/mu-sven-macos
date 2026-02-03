extends Node3D

class_name MUTerrain

const TERRAIN_SIZE = 256

@export var world_id: int = 1
@export var data_path: String = "res://reference/MuMain/src/bin/Data/World1"

const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUTerrainMeshBuilder = preload("res://addons/mu_tools/mu_terrain_mesh_builder.gd")

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
	var height_file = data_path.path_join("TerrainHeight.OZB")
	var mapping_file = data_path.path_join("EncTerrain%d.map" % world_id)
	var objects_file = data_path.path_join("EncTerrain%d.obj" % world_id)
	
	print("[MUTerrain] Loading World %d..." % world_id)
	
	heightmap = parser.parse_height_file(height_file)
	map_data = parser.parse_mapping_file(mapping_file)
	object_data = parser.parse_objects_file(objects_file)
	
	# 4. Load Attributes
	var att_path = data_path.path_join("EncTerrain%d.att" % world_id)
	attributes = parser.parse_attributes_file(att_path)
	print("[MUTerrain] Loaded attributes: %d bytes" % attributes.size())
	
	# 5. Load Lightmap (TerrainLight.OZJ)
	var light_path = data_path.path_join("TerrainLight.OZJ")
	const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
	var light_tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(light_path))
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
	add_child(terrain_mesh)
	
	# 6. Setup Material
	_setup_material()
	
	# 7. Create Water Mesh
	_create_water_mesh()
	
	# 8. Create Grass Mesh
	_create_grass_mesh()
	
	# 9. Spawn Objects
	_spawn_objects()


func _setup_material():
	print("[MUTerrain] Loading tile textures...")
	
	# SVEN texture order (MapManager.cpp lines 1364-1447)
	var texture_names = [
		"TileGrass01",    # Index 0 - BITMAP_MAPTILE + 0
		"TileGrass02",    # Index 1 - BITMAP_MAPTILE + 1
		"TileGround01",   # Index 2 - BITMAP_MAPTILE + 2
		"TileGround02",   # Index 3 - BITMAP_MAPTILE + 3
		"TileGround03",   # Index 4 - BITMAP_MAPTILE + 4
		"TileWater01",    # Index 5 - BITMAP_MAPTILE + 5
		"TileWood01",     # Index 6 - BITMAP_MAPTILE + 6
		"TileRock01",     # Index 7 - BITMAP_MAPTILE + 7
		"TileRock02",     # Index 8 - BITMAP_MAPTILE + 8
		"TileRock03",     # Index 9 - BITMAP_MAPTILE + 9
		"TileRock04",     # Index 10 - BITMAP_MAPTILE + 10
		"TileRock05",     # Index 11 - BITMAP_MAPTILE + 11
		"TileRock06",     # Index 12 - BITMAP_MAPTILE + 12
		"TileRock07",     # Index 13 - BITMAP_MAPTILE + 13
	]
	
	const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
	var textures: Array[Image] = []
	
	for i in range(texture_names.size()):
		var tex_name = texture_names[i]
		var loaded = false
		
		# Try .OZJ first, then .OZT, then .jpg
		for ext in [".OZJ", ".OZT", ".jpg"]:
			var path = data_path.path_join(tex_name + ext)
			
			# Try Godot's load first
			var tex = load(path) as Texture2D
			if tex:
				var img = tex.get_image()
				textures.append(img)
				print("  [%d] Loaded: %s (%dx%d)" % [i, tex_name + ext, img.get_width(), img.get_height()])
				loaded = true
				break
			
			# Try direct loader
			var direct_tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(path))
			if direct_tex:
				var img = direct_tex.get_image()
				textures.append(img)
				print("  [%d] Loaded (direct): %s (%dx%d)" % [i, tex_name + ext, img.get_width(), img.get_height()])
				loaded = true
				break
		
		if not loaded:
			# Create fallback texture
			var img = Image.create(256, 256, false, Image.FORMAT_RGB8)
			img.fill(Color(0.5, 0.5, 0.5))  # Gray fallback
			textures.append(img)
			print("  [%d] FALLBACK: %s (gray 256x256)" % [i, tex_name])
	
	# Create Texture2DArray
	print("[MUTerrain] Creating Texture2DArray with %d textures..." % textures.size())
	
	# Find max size for array (all must be same size)
	var max_size = 256
	for img in textures:
		max_size = max(max_size, max(img.get_width(), img.get_height()))
	
	# Resize all to max_size
	for i in range(textures.size()):
		var img = textures[i]
		if img.get_width() != max_size or img.get_height() != max_size:
			print("  Resizing texture from (%d, %d) to %dx%d" % [img.get_width(), img.get_height(), max_size, max_size])
			img.resize(max_size, max_size, Image.INTERPOLATE_LANCZOS)
	
	var tex_array = Texture2DArray.new()
	tex_array.create_from_images(textures)
	
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
				var idx = y * TERRAIN_SIZE + x
				layer1_img.set_pixel(x, y, Color(map_data.layer1[idx] / 255.0, 0, 0))
				layer2_img.set_pixel(x, y, Color(map_data.layer2[idx] / 255.0, 0, 0))
				alpha_img.set_pixel(x, y, Color(map_data.alpha[idx], 0, 0))
		
		mat.set_shader_parameter("layer1_map", ImageTexture.create_from_image(layer1_img))
		mat.set_shader_parameter("layer2_map", ImageTexture.create_from_image(layer2_img))
		mat.set_shader_parameter("alpha_map", ImageTexture.create_from_image(alpha_img))
	
	terrain_mesh.material_override = mat


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
		var v0 = Vector3(x, h, y)
		var v1 = Vector3(x + 1, h, y)
		var v2 = Vector3(x + 1, h, y + 1)
		var v3 = Vector3(x, h, y + 1)
		
		var uv0 = Vector2(0, 0)
		var uv1 = Vector2(1, 0)
		var uv2 = Vector2(1, 1)
		var uv3 = Vector2(0, 1)
		
		var normal = Vector3.UP
		var color = Color.WHITE  # White vertex color for proper lighting
		
		# Triangle 1
		st.set_normal(normal)
		st.set_color(color)
		st.set_uv(uv0)
		st.add_vertex(v0)
		st.set_uv(uv1)
		st.add_vertex(v1)
		st.set_uv(uv2)
		st.add_vertex(v2)
		
		# Triangle 2
		st.set_uv(uv0)
		st.add_vertex(v0)
		st.set_uv(uv2)
		st.add_vertex(v2)
		st.set_uv(uv3)
		st.add_vertex(v3)
	
	var mesh = st.commit()
	
	# Load water texture - try multiple paths
	var water_tex: Texture2D = null
	var tex_paths = [
		data_path.path_join("TileWater01.OZJ"),
		data_path.path_join("TileWater01.OZT"),
	]
	
	for path in tex_paths:
		var tex = load(path) as Texture2D
		if tex:
			water_tex = tex
			print("  Loaded water texture: %s" % path)
			break
	
	# Try direct texture loader as fallback
	if not water_tex:
		for path in tex_paths:
			const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
			var tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(path))
			if tex:
				water_tex = tex
				print("  Loaded water texture (direct): %s" % path)
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
	
	# Create FLAT grass quad (horizontal on terrain, not vertical billboard!)
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	# Flat quad lying on terrain surface (1m x 1m)
	var size = 1.0
	var v0 = Vector3(-size/2, 0.05, -size/2)  # Slight raise (5cm) above terrain
	var v1 = Vector3(size/2, 0.05, -size/2)
	var v2 = Vector3(size/2, 0.05, size/2)
	var v3 = Vector3(-size/2, 0.05, size/2)
	
	# UVs for full texture (we'll use texture atlas in shader if needed)
	var uv0 = Vector2(0, 0)
	var uv1 = Vector2(1, 0)
	var uv2 = Vector2(1, 1)
	var uv3 = Vector2(0, 1)
	
	var normal = Vector3.UP
	
	# Triangle 1
	st.set_normal(normal)
	st.set_uv(uv0)
	st.add_vertex(v0)
	st.set_uv(uv1)
	st.add_vertex(v1)
	st.set_uv(uv2)
	st.add_vertex(v2)
	
	# Triangle 2
	st.set_uv(uv0)
	st.add_vertex(v0)
	st.set_uv(uv2)
	st.add_vertex(v2)
	st.set_uv(uv3)
	st.add_vertex(v3)
	
	var grass_quad = st.commit()
	
	# Create MultiMesh for instancing
	var multi_mesh = MultiMesh.new()
	multi_mesh.transform_format = MultiMesh.TRANSFORM_3D
	multi_mesh.use_colors = true
	multi_mesh.mesh = grass_quad
	multi_mesh.instance_count = map_data.grass_tiles.size()
	
	# Position each grass instance
	for i in range(map_data.grass_tiles.size()):
		var tile = map_data.grass_tiles[i]
		var x = tile.x
		var y = tile.y
		
		# Get height at this position
		var idx = y * TERRAIN_SIZE + x
		var tile_h = heightmap[idx] if idx < heightmap.size() else 0.0
		
		# Position at tile center
		var transform = Transform3D()
		transform.origin = Vector3(x + 0.5, tile_h, y + 0.5)
		
		multi_mesh.set_instance_transform(i, transform)
		multi_mesh.set_instance_color(i, Color.WHITE)  # White for proper lighting
	
	# Load grass texture - SVEN uses .tga for billboards (BITMAP_MAPGRASS)
	var grass_tex: Texture2D = null
	var tex_paths = [
		data_path.path_join("TileGrass01.tga"),  # Correct billboard texture!
		data_path.path_join("TileGrass01.OZT"),  # Fallback
	]
	
	for path in tex_paths:
		var tex = load(path) as Texture2D
		if tex:
			grass_tex = tex
			print("  Loaded grass texture: %s" % path)
			break
	
	# Try direct texture loader as fallback
	if not grass_tex:
		for path in tex_paths:
			const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
			var tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(path))
			if tex:
				grass_tex = tex
				print("  Loaded grass texture (direct): %s" % path)
				break
	
	# Create grass material with wind shader
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/grass_billboard.gdshader")
	if grass_tex:
		mat.set_shader_parameter("grass_texture", grass_tex)
	else:
		print("  Using fallback green color for grass")
	

	grass_mesh = MultiMeshInstance3D.new()
	grass_mesh.multimesh = multi_mesh
	grass_mesh.material_override = mat
	grass_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(grass_mesh)
	
	print("[MUTerrain] Grass mesh created with %d instances!" % multi_mesh.instance_count)

func _spawn_objects():
	print("[MUTerrain] Spawning %d objects..." % object_data.size())
	# We'll need a way to map object types to BMD files
	# For now, just print or spawn placeholders
	for obj in object_data:
		# TODO: Implement object spawning logic
		pass

func is_walkable(tile_x: int, tile_y: int) -> bool:
	"""Check if a tile is walkable based on attributes"""
	if attributes.is_empty():
		return true  # No attributes loaded, assume walkable
	
	if tile_x < 0 or tile_x >= TERRAIN_SIZE or tile_y < 0 or tile_y >= TERRAIN_SIZE:
		return false  # Out of bounds
	
	var idx = tile_y * TERRAIN_SIZE + tile_x
	var attr = attributes[idx]
	
	# Attribute 0 = walkable, 1 = blocked, 2+ = special zones
	return attr == 0 or attr == 2  # Walkable or safe zone

func get_attribute(tile_x: int, tile_y: int) -> int:
	"""Get attribute value for a tile"""
	if attributes.is_empty():
		return 0
	
	if tile_x < 0 or tile_x >= TERRAIN_SIZE or tile_y < 0 or tile_y >= TERRAIN_SIZE:
		return 1  # Out of bounds = blocked
	
	var idx = tile_y * TERRAIN_SIZE + tile_x
	return attributes[idx]
