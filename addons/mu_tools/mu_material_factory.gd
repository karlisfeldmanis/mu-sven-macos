@tool
class_name MUMaterialFactory

## Generates MU-specific materials (Phase 0)
## Handles Alpha Scissor, Cull Mode, and Glow Shaders.

static func create_material(texture: Texture2D, flags: int = 0) -> ShaderMaterial:
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/mu_character.gdshader")
	
	mat.set_shader_parameter("albedo_texture", texture)
	
	# Flag logic based on MuMain bits (e.g., flags & 1 for hair/transparency)
	if flags & 0x01:
		mat.set_shader_parameter("use_alpha_scissor", true)
		
	return mat
