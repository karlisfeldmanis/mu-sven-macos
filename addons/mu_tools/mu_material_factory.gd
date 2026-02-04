@tool
class_name MUMaterialFactory

## Generates MU-specific materials (Phase 0)
## Handles Alpha Scissor, Cull Mode, and Glow Shaders.

static func create_material(texture: Texture2D, flags: int = 0, texture_name: String = "") -> ShaderMaterial:
	var mat = ShaderMaterial.new()
	mat.shader = load("res://core/shaders/mu_character.gdshader")
	
	mat.set_shader_parameter("albedo_texture", texture)
	
	# Use alpha blending for MU textures (not alpha scissor)
	# Many MU textures have semi-transparent pixels that need blending
	mat.set_shader_parameter("use_alpha_scissor", false)
	
	# Flag logic based on MuMain bits
	if flags & 0x01:
		# Hair/special effects - use alpha scissor for sharp cutouts
		mat.set_shader_parameter("use_alpha_scissor", true)
		mat.set_shader_parameter("alpha_test_threshold", 0.5)
	
	# 2. Heuristic: Transparency detection by name
	var name_lower = texture_name.to_lower()
	var is_opaque_format = name_lower.ends_with(".jpg") or name_lower.ends_with(".ozj")
	
	# Additive blending (Glows/Skills)
	# MuMain: often bit 0x02 or 0x01 depending on version
	var is_additive = (flags & 0x02) != 0 or \
						name_lower.contains("glow") or \
						name_lower.contains("effect") or \
						name_lower.contains("light")
	
	if is_additive:
		print("  [MUMaterialFactory] Enabled Additive Blending for: ", texture_name)
		mat.set_shader_parameter("is_additive", true)
		# Additive things shouldn't use alpha scissor
		mat.set_shader_parameter("use_alpha_scissor", false)
	
	# Cutout transparency (Sharp edges, no blending)
	var is_cutout = name_lower.contains("tree") or \
						 name_lower.contains("grass") or \
						 name_lower.contains("leaf") or \
						 name_lower.contains("fence") or \
						 name_lower.contains("gate")
						
	# Blended transparency (Semi-transparent, smooth edges)
	var is_blended = name_lower.contains("shadow") or \
						  name_lower.contains("water") or \
						  is_additive
						
	# Only enable alpha scissor if it's a cutout type AND NOT a JPG/OZJ
	if is_cutout and not is_opaque_format and not is_additive:
		print("  [MUMaterialFactory] Enabled Alpha Scissor for: ", texture_name)
		mat.set_shader_parameter("use_alpha_scissor", true)
		mat.set_shader_parameter("alpha_test_threshold", 0.05)
	
	# If it's a blended type, we ensure alpha scissor is OFF (default)
	# and the shader handles it via blend_mix.
	if is_blended:
		print("  [MUMaterialFactory] Using Blending for: ", texture_name)
		mat.set_shader_parameter("use_alpha_scissor", false)
		
	return mat
