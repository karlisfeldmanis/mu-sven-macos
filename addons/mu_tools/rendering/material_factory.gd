@tool
# class_name MUMaterialFactory

## Generates MU-specific materials (Phase 0)
## Handles Alpha Scissor, Cull Mode, and Glow Shaders.

const MUMaterialHelper = preload("res://addons/mu_tools/rendering/material_helper.gd")

static func create_material(
		texture: Texture2D, 
		_flags: int = 0, 
		texture_name: String = "",
		model_path: String = "",
		surface_index: int = 0,
		animated_materials: Array = []) -> StandardMaterial3D:
	var mat = StandardMaterial3D.new()
	mat.resource_name = texture_name
	mat.albedo_texture = texture
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	
	# Use centralized helper for all effect logic
	MUMaterialHelper.setup_material(mat, surface_index, model_path, texture_name, animated_materials)
	
	return mat
