@tool
extends Node

# class_name MUMaterialHelper

## Centralized utility for MU Online material effects.
## Bridges the gap between raw BMD textures and Godot material properties.


# const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd") # Dynamic load

const MURenderRules = preload("res://addons/mu_tools/core/render_rules.gd")
const MUMeshShader = preload("res://addons/mu_tools/shaders/mu_mesh.gdshader")
const MUMeshBrightShader = preload("res://addons/mu_tools/shaders/mu_mesh_bright.gdshader")

static var _registry = null
static func get_registry():
	if not _registry:
		_registry = load("res://addons/mu_tools/core/registry.gd")
	return _registry

## Configures a material with MU-standard effects (Transparency, Blending, Scrolling).
## Works with both StandardMaterial3D (Viewer) and ShaderMaterial (Game).
static func setup_material(mat: Material, surface_index: int, model_path: String,
		texture_name: String = "", animated_materials: Array = []) -> void:
	if not mat: return
	
	var texture_name_lower = texture_name.to_lower()
	
	# 1. Resolve Metadata from Registry
	var meta = get_registry().get_metadata(model_path)
	var scroll_uv = meta.get("scroll_uv", [])
	var scroll_index = int(meta.get("scroll_uv_index", -1))
	
	# 2. Texture Script Parsing (Sven Parity)
	var script = parse_texture_script(texture_name)
	
	# 3. Effect Detection (Heuristics)
	var should_scroll = false
	if scroll_index != -1 and surface_index == scroll_index:
		should_scroll = true
	# Also check _S script
	if script.get("stream", false):
		should_scroll = true
	
	var is_wave = false # Default to false. Explicitly set via RenderRules or Registry metadata.
	
	var is_bright = script.get("bright", false)
	if not is_bright:
		for kw in get_registry().EFFECT_KEYWORDS:
			if kw in texture_name_lower:
				is_bright = true
				break
	
	var is_effect = is_bright
	var is_hidden = script.get("hidden", false)
	var is_none_blend = script.get("none_blend", false)
	
	var is_shadow = false
	for kw in get_registry().SHADOW_KEYWORDS:
		if kw in texture_name_lower:
			is_shadow = true
			break
			
	var is_cutout = false
	for kw in get_registry().CUTOUT_KEYWORDS:
		if kw in texture_name_lower:
			is_cutout = true
			break
	
	var is_chrome = false
	if "chrome" in texture_name_lower or "metal" in texture_name_lower:
		is_chrome = true
	
	# Smarter cutout detection: Check actual texture alpha channel
	if not is_cutout and not is_none_blend and mat is BaseMaterial3D and mat.albedo_texture:
		var img = mat.albedo_texture.get_image()
		if img and img.detect_alpha() != Image.ALPHA_NONE:
			is_cutout = true
	
	var is_ignored = is_none_blend
	if not is_ignored:
		for kw in get_registry().IGNORE_ALPHA_KEYWORDS:
			if kw in texture_name_lower:
				is_ignored = true
				break
	
	# scrolling or effect means special
	var is_special = (is_effect or is_shadow or should_scroll or is_wave or is_chrome) \
			and not is_ignored
	
	# 4. Detect Semantic Alpha (Implicit Alpha rule)
	var has_alpha = false
	if mat is BaseMaterial3D and mat.albedo_texture:
		# Image.detect_alpha() is expensive, but we only do it once per material creation
		var img = mat.albedo_texture.get_image()
		if img and img.detect_alpha() != Image.ALPHA_NONE:
			has_alpha = true
	elif mat is ShaderMaterial:
		var tex = mat.get_shader_parameter("albedo_texture")
		if tex and tex is Texture2D:
			var img = tex.get_image()
			if img and img.detect_alpha() != Image.ALPHA_NONE:
				has_alpha = true
	
	# SVEN Parity: JPG (.ozj) textures NEVER have alpha.
	# Ignore "detect_alpha" false positives.
	if texture_name_lower.ends_with(".ozj"):
		has_alpha = false
		is_cutout = false

	# SVEN Parity: Global transparency check for world objects only.
	# "only for objects, no characters or monsters" - based on model_path containing "Object"
	var is_world_object = model_path.contains("Object")
	
	# 5. Apply Properties
	if mat is BaseMaterial3D:
		_setup_base_material(mat, is_special, is_shadow, is_cutout or has_alpha, texture_name_lower, is_chrome)
	elif mat is ShaderMaterial:
		# SVEN Parity: Use 0.25 threshold for alpha test
		_setup_mu_shader_material(mat, is_special or has_alpha, is_shadow, \
				is_cutout or has_alpha, is_wave, is_chrome, is_bright, \
				should_scroll, scroll_uv)
		# 6. Apply SVEN Hardcoded Rules (May swap shader)
		MURenderRules.apply_rules_to_mesh(surface_index, model_path, mat)
		
		# 7. Final Shader Validation (In case RenderRules or TextureScript want Bright)
		ensure_correct_shader(mat)
	
	# Store script metadata for higher-level logic (e.g. mesh visibility)
	mat.set_meta("mu_script_hidden", is_hidden)
	
	# 5. Handle UV Animation Registration
	if should_scroll and not scroll_uv.is_empty() and scroll_uv.size() == 2:
		animated_materials.append({
			"material": mat,
			"speed": Vector2(scroll_uv[0], scroll_uv[1])
		})
	
	# 6. Apply Manual Tweaks from Registry
	var mat_tweak = get_registry().get_material_tweak(texture_name_lower)
	if not mat_tweak.is_empty():
		_apply_manual_tweaks(mat, mat_tweak)

## Parsers MU "Texture Scripts" embedded in filenames (ZzzBMD.cpp:2834 parsingTScriptA)
## Patterns: _R (Bright), _H (Hidden), _S (Stream/Wave), _N (NoneBlend/Opaque)
static func parse_texture_script(filename: String) -> Dictionary:
	var script = {"bright": false, "hidden": false, "stream": false, "none_blend": false}
	var last_underscore = filename.rfind("_")
	if last_underscore == -1: return script
	
	var base_name = filename.get_basename().to_upper()
	var suffix = base_name.substr(last_underscore + 1)
	
	# Strict validation: suffix must consist only of valid script tokens (R, H, S, N)
	# and be reasonably short (usually 1-2 chars)
	if suffix.is_empty() or suffix.length() > 3:
		return script
		
	for i in range(suffix.length()):
		if not suffix[i] in "RHSN":
			return script # Not a valid script suffix
	
	if "R" in suffix: script["bright"] = true
	if "H" in suffix: script["hidden"] = true
	if "S" in suffix: script["stream"] = true
	if "N" in suffix: script["none_blend"] = true
	
	return script

static func _setup_base_material(mat: BaseMaterial3D, is_special: bool, is_shadow: bool,
		is_cutout: bool, texture_name_lower: String, is_chrome: bool = false) -> void:
	# SVEN Logic: EnableAlphaTest and EnableAlphaBlend both DisableCullFace
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	
	if is_chrome:
		mat.metallic = 1.0
		mat.roughness = 0.05 # Even smoother for MU parity
		mat.specular = 1.0
		mat.albedo_color = Color(1.0, 1.0, 1.0)
	
	var is_environment = false
	for kw in ["grass", "tree", "leaf"]:
		if kw in texture_name_lower:
			is_environment = true
			break
			
	if is_special:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		if is_shadow:
			mat.blend_mode = BaseMaterial3D.BLEND_MODE_MIX
		else:
			# Water, Fire, Lights use Additive
			mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	elif is_cutout or is_environment:
		# SVEN uses alpha test + blending for everything with alpha
		# Threshold in SVEN is 0.25 (line 617 ZzzOpenglUtil.cpp)
		# NOTE: Environment (Trees/Grass) MUST use Scissor to maintain depth buffer.
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		mat.alpha_scissor_threshold = 0.25
	else:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED

static func _setup_mu_shader_material(mat: ShaderMaterial, _is_special: bool, _is_shadow: bool,
		is_cutout: bool, is_wave: bool, is_chrome: bool, is_bright: bool,
		should_scroll: bool, scroll_uv: Array) -> void:
	mat.set_shader_parameter("use_wave", is_wave)
	mat.set_shader_parameter("use_chrome", is_chrome)
	mat.set_shader_parameter("use_bright", is_bright)
	mat.set_shader_parameter("use_pulsing", false) # Default, overridden by rules
	mat.set_shader_parameter("use_sawtooth", false) # Default, overridden by rules
	mat.set_shader_parameter("use_jitter", false) # Default, overridden by rules
	
	if should_scroll and not scroll_uv.is_empty():
		mat.set_shader_parameter("scroll_speed_u", scroll_uv[0])
		mat.set_shader_parameter("scroll_speed_v", scroll_uv[1])
	
	if is_cutout:
		mat.set_shader_parameter("alpha_scissor_threshold", 0.25)
	else:
		mat.set_shader_parameter("alpha_scissor_threshold", 0.0)

static func ensure_correct_shader(mat: ShaderMaterial) -> void:
	var use_bright = mat.get_shader_parameter("use_bright")
	if use_bright and mat.shader != MUMeshBrightShader:
		mat.shader = MUMeshBrightShader
	elif not use_bright and mat.shader != MUMeshShader:
		mat.shader = MUMeshShader

static func _apply_manual_tweaks(mat: Material, tweak: Dictionary) -> void:
	if mat is BaseMaterial3D:
		if tweak.has("transparency"):
			mat.transparency = tweak["transparency"]
		if tweak.has("blend_mode"):
			mat.blend_mode = tweak["blend_mode"]
		if tweak.has("cull_mode"):
			mat.cull_mode = tweak["cull_mode"]
		if tweak.has("alpha_scissor_threshold"):
			mat.alpha_scissor_threshold = tweak["alpha_scissor_threshold"]
	elif mat is ShaderMaterial:
		# Map tweaks to shader params if needed
		if tweak.has("chrome"):
			mat.set_shader_parameter("use_chrome", tweak["chrome"])
		if tweak.has("wave"):
			mat.set_shader_parameter("use_wave", tweak["wave"])
		if tweak.has("bright"):
			mat.set_shader_parameter("use_bright", tweak["bright"])
