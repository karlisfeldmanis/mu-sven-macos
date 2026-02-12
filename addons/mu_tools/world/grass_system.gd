@tool
extends Node3D

## GrassSystem
## Handles all grass rendering (MultiMesh BMDs and Procedural Billboards).

# class_name MUGrassSystem

const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/rendering/bmd_mesh_builder.gd")
const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")

func spawn_grass(heightmap: Node3D):
	if not heightmap: return
	
	var objects = heightmap.get_objects_data()
	var world_id = heightmap.world_id
	
	_spawn_multimesh_grass(objects, world_id, heightmap)
	_spawn_procedural_grass(heightmap)

func _spawn_multimesh_grass(objects: Array, world_id: int, heightmap: Node3D):
	const GRASS_MIN = 20
	const GRASS_MAX = 27
	
	var buckets = {} # bmd_path -> Array[instances]
	var city_filter = Rect2(95, 95, 75, 75) # Standardized (G_X=MU_Y, G_Z=MU_X)
	
	for obj in objects:
		if obj.type < GRASS_MIN or obj.type > GRASS_MAX:
			continue
		
		# Spatial Filter check (Removed to allow global grass)
		# if not city_filter.has_point(Vector2(obj.position.x, obj.position.z)):
		# 	continue
			
		var path = MUModelRegistry.get_object_path(obj.type, world_id)
		if path == "": continue
		
		if not buckets.has(path):
			buckets[path] = []
		buckets[path].append(obj)
		
	var heights = heightmap.get_height_data()
	
	for bmd_path in buckets:
		var insts = buckets[bmd_path]
		var parser = BMDParser.new()
		if not parser.parse_file(ProjectSettings.globalize_path(bmd_path)):
			continue
			
		var mm = MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.instance_count = insts.size()
		
		var mmi = MultiMeshInstance3D.new()
		mmi.multimesh = mm
		mmi.name = "Grass_" + bmd_path.get_file().get_basename()
		add_child(mmi)
		
		# Build source mesh
		var temp_mi = MUMeshBuilder.create_mesh_instance(
			parser.meshes[0], null, bmd_path, parser, true
		)
		var source_mesh = temp_mi.mesh
		var source_mat = source_mesh.surface_get_material(0)
		temp_mi.queue_free()
		
		mm.mesh = source_mesh
		
		# Apply Alpha Scissor Material Override
		var grass_mat = source_mat.duplicate()
		grass_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		grass_mat.alpha_scissor_threshold = 0.5
		grass_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
		mmi.material_override = grass_mat
		
		for i in range(insts.size()):
			var inst = insts[i]
			var t = Transform3D()
			var sc_vec = MUCoordinateUtils.convert_scale(inst.scale)
			t = t.scaled(sc_vec)
			t.basis = Basis(inst.rotation) * t.basis
			
			# Height snapping
			var final_pos = inst.position
			if not heights.is_empty():
				var ground_h = heightmap.get_height_at_world(final_pos)
				final_pos.y = ground_h
				
			t.origin = final_pos
			mm.set_instance_transform(i, t)
		
		print("  [GrassSystem] Instanced %d x %s" % [insts.size(), bmd_path.get_file()])

## Procedural Terrain Grass (SVEN ZzzLodTerrain.cpp 1:1 Migration)
## Generates billboard quads on terrain tiles where Layer1 is a grass texture.
func _spawn_procedural_grass(heightmap: Node3D):
	var grass_tiles = heightmap.get_grass_tiles()
	if grass_tiles.is_empty():
		print("  [GrassSystem] No procedural grass tiles found.")
		return

	var heights = heightmap.get_height_data()
	if heights.is_empty():
		push_error("[GrassSystem] No height data for procedural grass")
		return

	var world_id = heightmap.world_id
	var data_path = heightmap.data_path
	var world_dir = "World" + str(world_id + 1)
	var world_path = data_path.path_join(world_dir)

	# Get per-row random atlas offset (256x1 image, R channel = rand()%4 * 0.25)
	var grass_offset_tex = heightmap.get_grass_offset_texture()
	var grass_offset_img: Image = null
	if grass_offset_tex:
		grass_offset_img = grass_offset_tex.get_image()

	var lightmap_tex = heightmap.get_lightmap_texture()
	var mapping_alpha = heightmap.get_mapping_alpha()

	for grass_type in grass_tiles:
		var tiles: Array = grass_tiles[grass_type]
		if tiles.is_empty():
			continue

		# SVEN: BITMAP_MAPGRASS + TerrainMappingLayer1[index]
		# Type 0 → TileGrass01, Type 1 → TileGrass02
		var tex_name = "TileGrass%02d" % (grass_type + 1)
		var grass_tex = _load_grass_texture(world_path, tex_name)
		if not grass_tex:
			push_warning("[GrassSystem] Failed to load %s" % tex_name)
			continue

		# SVEN: float Height = pBitmap->Height * 2.f (MU units)
		var tex_img = grass_tex.get_image()
		var tex_height_px = tex_img.get_height()
		var grass_height_godot = float(tex_height_px) * 2.0 / 100.0

		var mesh = _build_procedural_grass_mesh(
			tiles, heights, grass_height_godot, grass_offset_img, mapping_alpha
		)

		if mesh:
			var mi = MeshInstance3D.new()
			mi.mesh = mesh
			mi.name = "ProceduralGrass_%s" % tex_name

			var mat = ShaderMaterial.new()
			mat.shader = preload("res://addons/mu_tools/shaders/procedural_grass.gdshader")
			mat.set_shader_parameter("grass_texture", grass_tex)
			if lightmap_tex:
				mat.set_shader_parameter("lightmap_texture", lightmap_tex)

			mi.material_override = mat
			add_child(mi)

			print("  [GrassSystem] Procedural %s: %d quads (H=%.2f)" % [
				tex_name, tiles.size(), grass_height_godot
			])

func _load_grass_texture(world_path: String, tex_name: String) -> Texture2D:
	for ext in ["OZT", "OZJ", "tga", "jpg"]:
		var path = world_path.path_join(tex_name + "." + ext)
		if FileAccess.file_exists(path):
			var tex = MUAPI.render().load_mu_texture(
				ProjectSettings.globalize_path(path)
			)
			if tex:
				return tex
	return null

func _build_procedural_grass_mesh(
	tiles: Array,
	heights: PackedFloat32Array,
	grass_height: float,
	grass_offset_img: Image,
	mapping_alpha: PackedFloat32Array
) -> ArrayMesh:
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)

	const TERRAIN_SIZE = 256
	const ATLAS_WIDTH = 0.25 # 4 columns: 64/256
	var has_alpha = not mapping_alpha.is_empty()
	var skipped = 0

	for tile_pos in tiles:
		var mu_x: int = tile_pos.x
		var mu_y: int = tile_pos.y

		# SVEN Strict 4-Corner Alpha Check (ZzzLodTerrain.cpp:1612-1614)
		# If ANY corner of the tile has alpha > 0, skip grass entirely
		if has_alpha:
			var cx1 = mini(mu_x + 1, TERRAIN_SIZE - 1)
			var cy1 = mini(mu_y + 1, TERRAIN_SIZE - 1)
			var a1 = mapping_alpha[mu_y * TERRAIN_SIZE + mu_x]
			var a2 = mapping_alpha[mu_y * TERRAIN_SIZE + cx1]
			var a3 = mapping_alpha[cy1 * TERRAIN_SIZE + cx1]
			var a4 = mapping_alpha[cy1 * TERRAIN_SIZE + mu_x]
			if a1 > 0.0 or a2 > 0.0 or a3 > 0.0 or a4 > 0.0:
				skipped += 1
				continue

		# Height at tile corner (mu_x, mu_y)
		var idx_00 = mu_y * TERRAIN_SIZE + mu_x
		var h0 = heights[idx_00]

		# Height at diagonal corner (mu_x+1, mu_y+1), clamped to grid
		var diag_mu_x = mini(mu_x + 1, TERRAIN_SIZE - 1)
		var diag_mu_y = mini(mu_y + 1, TERRAIN_SIZE - 1)
		var idx_11 = diag_mu_y * TERRAIN_SIZE + diag_mu_x
		var h_diag = heights[idx_11]

		# SVEN Quad: Diagonal strip from (xi,yi) to (xi+1,yi+1)
		# Coordinate mapping (terrain_mesh_builder.gd parity):
		#   Godot X = mu_y, Godot Z = mu_x, Godot Y = height

		# Bottom vertices (on terrain surface)
		var bl = Vector3(float(mu_y), h0, float(mu_x))
		var br = Vector3(float(mu_y + 1), h_diag, float(mu_x + 1))

		# Top vertices (raised + shear)
		# SVEN: TerrainVertex[n][2] += Height  (MU Z = Godot Y)
		# SVEN: TerrainVertex[n][0] += -50.f   (MU X = Godot Z, -50/100 = -0.5)
		var tl = Vector3(float(mu_y), h0 + grass_height, float(mu_x) - 0.5)
		var tr = Vector3(float(mu_y + 1), h_diag + grass_height, float(mu_x + 1) - 0.5)

		# Atlas UV (SVEN: su = xf * Width + TerrainGrassTexture[yi])
		var xf_col = mu_x % 4
		var row_offset = 0.0
		if grass_offset_img:
			var pixel = grass_offset_img.get_pixel(mu_y % TERRAIN_SIZE, 0)
			row_offset = pixel.r
		var su = float(xf_col) * ATLAS_WIDTH + row_offset

		# UV layout: top y=0, bottom y=1 (shader uses UV.y as height indicator)
		var uv_tl = Vector2(su, 0.0)
		var uv_tr = Vector2(su + ATLAS_WIDTH, 0.0)
		var uv_bl = Vector2(su, 1.0)
		var uv_br = Vector2(su + ATLAS_WIDTH, 1.0)

		# Triangle 1: top-left, bottom-left, bottom-right
		st.set_uv(uv_tl); st.add_vertex(tl)
		st.set_uv(uv_bl); st.add_vertex(bl)
		st.set_uv(uv_br); st.add_vertex(br)

		# Triangle 2: top-left, bottom-right, top-right
		st.set_uv(uv_tl); st.add_vertex(tl)
		st.set_uv(uv_br); st.add_vertex(br)
		st.set_uv(uv_tr); st.add_vertex(tr)

	if skipped > 0:
		print("  [GrassSystem] Filtered %d tiles (4-corner alpha check)" % skipped)

	var mesh = st.commit()
	mesh.custom_aabb = AABB(Vector3(0, -10, -1), Vector3(256, 20, 257))
	return mesh
