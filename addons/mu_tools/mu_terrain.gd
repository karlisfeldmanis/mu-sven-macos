extends Node3D

class_name MUTerrain

const TERRAIN_SIZE = 256

@export var world_id: int = 1
@export var data_path: String = "res://reference/MuMain/src/bin/Data/World1"

const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUTerrainMeshBuilder = preload("res://addons/mu_tools/mu_terrain_mesh_builder.gd")

var heightmap: PackedFloat32Array
var map_data: RefCounted # MUTerrainParser.MapData
var attributes: PackedByteArray # Collision/walkability data
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
	var light_file = data_path.path_join("TerrainLight.OZJ")
	
	print("[MUTerrain] Loading World %d..." % world_id)
	
	heightmap = parser.parse_height_file(height_file)
	map_data = parser.parse_mapping_file(mapping_file)
	object_data = parser.parse_objects_file(objects_file)
	
	# Load attributes (collision)
	var att_file = data_path.path_join("EncTerrain%d.att" % world_id)
	attributes = parser.parse_attributes_file(att_file)
	
	# 2. Load Lightmap
	var lightmap_tex = load(light_file) as Texture2D
	var lightmap_img: Image = null
	if lightmap_tex:
		lightmap_img = lightmap_tex.get_image()
	
	# 3. Build Mesh
	var mesh = mesh_builder.build_terrain_array_mesh(heightmap, lightmap_img)
	
	terrain_mesh = MeshInstance3D.new()
	terrain_mesh.mesh = mesh
	add_child(terrain_mesh)
	
	# 4. Setup Material
	_setup_material()
	
	# 5. Create Water Mesh
	_create_water_mesh()
	
	# 6. Create Grass Mesh
	_create_grass_mesh()
	
	# 7. Spawn Objects
	_spawn_objects()

func _setup_material():
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/mu_terrain.gdshader")
	
	# Create Data Textures for the shader
	var l1_img = Image.create_from_data(
		TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8, map_data.layer1
	)
	var l2_img = Image.create_from_data(
		TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8, map_data.layer2
	)
	
	var alpha_bytes = PackedByteArray()
	for a in map_data.alpha:
		alpha_bytes.append(int(a * 255.0))
	var alpha_img = Image.create_from_data(
		TERRAIN_SIZE, TERRAIN_SIZE, false, Image.FORMAT_R8, alpha_bytes
	)
	
	var l1_tex = ImageTexture.create_from_image(l1_img)
	var l2_tex = ImageTexture.create_from_image(l2_img)
	var a_tex = ImageTexture.create_from_image(alpha_img)
	
	mat.set_shader_parameter("layer1_map", l1_tex)
	mat.set_shader_parameter("layer2_map", l2_tex)
	mat.set_shader_parameter("alpha_map", a_tex)
	
	# Load Tile Textures into a Texture2DArray
	# MU mapping: 0, 1, 2, 3, 4 are standard tiles
	var tile_textures: Array[Texture2D] = []
	var tile_names = [
		"TileGrass01.OZJ",
		"TileGrass02.OZJ",
		"TileGround01.OZJ",
		"TileGround02.OZJ",
		"TileGround03.OZJ"
	]
	
	print("[MUTerrain] Loading tile textures...")
	for tname in tile_names:
		var tpath = data_path.path_join(tname)
		var tex = load(tpath) as Texture2D
		
		# Fallback: Direct load if not imported
		if not tex:
			const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
			tex = MUTextureLoader.load_mu_texture(ProjectSettings.globalize_path(tpath))
			
		if tex:
			print("  [%d] Loaded: %s (%dx%d)" % [tile_textures.size(), tname, tex.get_width(), tex.get_height()])
			tile_textures.append(tex)
		else:
			push_error("[MUTerrain] Failed to load tile: ", tpath)
			
	if not tile_textures.is_empty():
		print("[MUTerrain] Creating Texture2DArray with %d textures..." % tile_textures.size())
		var tex_array = Texture2DArray.new()
		# For simplicity, assume they are all the same size (256x256)
		# MU textures are usually 256x256
		var img_array: Array[Image] = []
		for t in tile_textures:
			var img = t.get_image()
			if img.get_size() != Vector2i(256, 256):
				print("  Resizing texture from %s to 256x256" % img.get_size())
				img.resize(256, 256)
			img_array.append(img)
			
		tex_array.create_from_images(img_array)
		mat.set_shader_parameter("tile_textures", tex_array)
		print("[MUTerrain] Texture2DArray created successfully!")
	else:
		push_error("[MUTerrain] No tile textures loaded!")
	
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
	
	# Create water material
	var mat = StandardMaterial3D.new()
	if water_tex:
		mat.albedo_texture = water_tex
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	else:
		# Fallback to blue if no texture
		mat.albedo_color = Color(0.3, 0.5, 0.8, 0.7)
		print("  Using fallback blue color for water")
	
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	
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
	
	# Load grass texture - try multiple paths
	var grass_tex: Texture2D = null
	var tex_paths = [
		data_path.path_join("TileGrass01.OZJ"),
		data_path.path_join("TileGrass01.OZT"),
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
	
	# Create grass material
	var mat = StandardMaterial3D.new()
	if grass_tex:
		mat.albedo_texture = grass_tex
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		mat.alpha_scissor_threshold = 0.5
	else:
		# Fallback to green if no texture
		mat.albedo_color = Color(0.4, 0.7, 0.3)
		print("  Using fallback green color for grass")
	
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED  # Use vertex colors
	
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
