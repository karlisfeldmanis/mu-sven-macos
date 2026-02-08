@tool
class_name MUMaterialFactory

## Generates MU-specific materials (Phase 0)
## Handles Alpha Scissor, Cull Mode, and Glow Shaders.

const MUMaterialHelper = preload("res://addons/mu_tools/core/mu_material_helper.gd")

static func create_material(
		texture: Texture2D, 
		_flags: int = 0, 
		texture_name: String = "",
		model_path: String = "",
		surface_index: int = 0) -> ShaderMaterial:
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/mu_character.gdshader")
	mat.resource_name = texture_name
	
	mat.set_shader_parameter("albedo_texture", texture)
	
	# Use centralized helper for all effect logic
	MUMaterialHelper.setup_material(mat, surface_index, model_path, texture_name)
	
	return mat
