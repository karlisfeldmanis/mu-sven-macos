@tool
class_name MUMaterialFactory

## Generates MU-specific materials (Phase 0)
## Handles Alpha Scissor, Cull Mode, and Glow Shaders.

static func create_material(
		texture: Texture2D, 
		flags: int = 0, 
		texture_name: String = "") -> ShaderMaterial:
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
	var is_additive = (flags & 0x02) != 0 or \
						name_lower.contains("effect") or \
						name_lower.contains("light")
	
	# Cutout transparency (Sharp edges, no blending)
	# Heuristic: "tree" is only a cutout if it's NOT a JPG/OZJ (which are usually trunks)
	var is_cutout = ((name_lower.contains("tree") and not is_opaque_format) or \
						 name_lower.contains("grass") or \
						 name_lower.contains("leaf") or \
						 name_lower.contains("fence") or \
						 name_lower.contains("gate"))
						
	# Blended transparency (Semi-transparent, smooth edges)
	var is_blended = name_lower.contains("shadow") or \
						  name_lower.contains("water") or \
						  is_additive

	# 3. Decision Logic
	
	if is_additive:
		print("  [MUMaterialFactory] Enabled Additive Blending for: ", texture_name)
		mat.set_shader_parameter("is_additive", true)
		# Additive things shouldn't use alpha scissor
		mat.set_shader_parameter("use_alpha_scissor", false)
	
	# Only enable alpha scissor if it's a cutout type
	elif is_cutout and not is_additive:
		print("  [MUMaterialFactory] Enabled Alpha Scissor for: ", texture_name)
		mat.set_shader_parameter("use_alpha_scissor", true)
		mat.set_shader_parameter("alpha_test_threshold", 0.05)
		
		# Enable wind sway for plants/grass
		mat.set_shader_parameter("use_wind", true)
		mat.set_shader_parameter("wind_strength", 1.0)
		
		# If it's a JPG/OZJ but marked as cutout, 
		# we might need the shader to treat black as transparent
		if is_opaque_format:
			mat.set_shader_parameter("black_is_transparent", true)
	
	# If it's a blended type (or generic TGA), we ensure alpha scissor is OFF (default)
	# and the shader handles it via blend_mix.
	# This ensures proper visibility for semi-transparent assets.
	else:
		if is_blended:
			print("  [MUMaterialFactory] Using Blending for: ", texture_name)
		mat.set_shader_parameter("use_alpha_scissor", false)
		
	return mat
