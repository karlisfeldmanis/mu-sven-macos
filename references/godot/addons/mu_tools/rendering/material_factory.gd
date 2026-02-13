@tool
# class_name MUMaterialFactory

## Generates MU-specific materials (Phase 0)
## Handles Alpha Scissor, Cull Mode, and Glow Shaders.

const MUMaterialHelper = preload("res://addons/mu_tools/rendering/material_helper.gd")
const MUMeshShader = preload("res://addons/mu_tools/shaders/mu_mesh.gdshader")

static func create_material(
		texture: Texture2D, 
		_flags: int = 0, 
		texture_name: String = "",
		model_path: String = "",
		surface_index: int = 0,
		animated_materials: Array = []) -> Material:
	
	var mat = ShaderMaterial.new()
	mat.shader = MUMeshShader
	mat.resource_name = texture_name
	
	# Initial parameters
	mat.set_shader_parameter("albedo_texture", texture)
	
	# Use centralized helper for all effect logic
	MUMaterialHelper.setup_material(mat, surface_index, model_path, texture_name, animated_materials)
	
	return mat
