extends Node
class_name MURenderAPI
# MURenderAPI.gd
# Interface for textures, materials, and lighting.

const MUTextureLoader = preload("res://addons/mu_tools/core/mu_texture_loader.gd")
const MUMaterialFactory = preload("res://addons/mu_tools/nodes/mu_material_factory.gd")
const MUEnvironmentScript = preload("res://addons/mu_tools/nodes/mu_environment.gd")

## Load a texture (OZJ/OZT Decryption included)
func load_mu_texture(path: String) -> Texture2D:
	return MUTextureLoader.load_mu_texture(path)

## Create a terrain shader material with the provided textures/data
func create_terrain_material(
	tex_array: Texture, 
	l1: Texture, 
	l2: Texture, 
	alpha: Texture, 
	offset: Texture, 
	lightmap: Texture = null,
	debug_view: int = 0, # Restore regular rendering
	scales: PackedFloat32Array = [], 
	symmetries: PackedFloat32Array = [], 
	categories: PackedFloat32Array = [],
	symmetry_tile_map: Texture = null
) -> ShaderMaterial:
	var mat = ShaderMaterial.new()
	mat.shader = preload("res://addons/mu_tools/shaders/mu_terrain.gdshader")
	
	# Create fallbacks for mandatory samples to avoid "No textures" black rendering
	var b_img = Image.create(4, 4, false, Image.FORMAT_RGBA8); b_img.fill(Color.BLACK)
	var w_img = Image.create(4, 4, false, Image.FORMAT_RGBA8); w_img.fill(Color.WHITE)
	var black = ImageTexture.create_from_image(b_img)
	var white = ImageTexture.create_from_image(w_img)
	
	mat.set_shader_parameter("tile_textures", tex_array)
	mat.set_shader_parameter("layer1_map", l1)
	mat.set_shader_parameter("layer2_map", l2)
	mat.set_shader_parameter("alpha_map", alpha)
	mat.set_shader_parameter("grass_offset_map", offset)
	mat.set_shader_parameter("lightmap_map", lightmap if lightmap else white) 
	mat.set_shader_parameter("shadow_map", white)
	mat.set_shader_parameter("water_map", black)
	mat.set_shader_parameter("symmetry_tile_map", symmetry_tile_map if symmetry_tile_map else black)
	mat.set_shader_parameter("debug_view", debug_view) 
	
	# 1. Symmetry & Scale Map (Unified API)
	var symmetry_data = symmetries
	var scale_data = scales
	var category_data = categories
	
	if symmetry_data.is_empty():
		symmetry_data.resize(256); symmetry_data.fill(0.0)
	if scale_data.is_empty():
		scale_data.resize(256); scale_data.fill(0.25)
	if category_data.is_empty():
		category_data.resize(256); category_data.fill(1.0) 
	
	# 2. Build Unified Data LUT (256x1 Texture)
	# R: Scale, G: Symmetry, B: Category
	var lut_img = Image.create(256, 1, false, Image.FORMAT_RGBAF)
	for i in range(256):
		lut_img.set_pixel(i, 0, Color(scale_data[i], symmetry_data[i], category_data[i], 1.0))
		
	var lut_tex = ImageTexture.create_from_image(lut_img)
	mat.set_shader_parameter("terrain_data_lut", lut_tex)
	mat.set_shader_parameter("highlight_index", -1)
	
	return mat

## Setup global lighting and environment for a world
func setup_world_environment(node: Node, world_id: int):
	var env = MUEnvironmentScript.new()
	node.add_child(env)
	env.setup_environment(world_id)

## Apply standard character shader to a material
func apply_character_shader(material: ShaderMaterial, texture: Texture2D):
	material.shader = load("res://addons/mu_tools/shaders/mu_character.gdshader")
	material.set_shader_parameter("main_texture", texture)
