@tool
extends Node

class_name MUMaterialHelper

## Centralized utility for MU Online material effects.
## Bridges the gap between raw BMD textures and Godot material properties.


# const MUModelRegistry = preload("res://addons/mu_tools/core/mu_model_registry.gd") # Dynamic load

static var _registry = null
static func get_registry():
	if not _registry:
		_registry = load("res://addons/mu_tools/core/mu_model_registry.gd")
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
	
	var is_wave = false
	var wave_idx = int(meta.get("blend_mesh_index", -1))
	if wave_idx != -1 and surface_index == wave_idx:
		is_wave = true
	
	var is_bright = script.get("bright", false)
	var is_hidden = script.get("hidden", false)
	var is_none_blend = script.get("none_blend", false)
	
	var is_effect = is_bright
	if not is_effect:
		for kw in get_registry().EFFECT_KEYWORDS:
			if kw in texture_name_lower:
				is_effect = true
				break
	
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
	
	# 4. Apply Properties
	if mat is BaseMaterial3D:
		_setup_base_material(mat, is_special, is_shadow, is_cutout, texture_name_lower, is_chrome)
	elif mat is ShaderMaterial:
		_setup_shader_material(mat, is_special, is_shadow, is_cutout, is_wave)
	
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
	var underscore_pos = filename.rfind("_")
	if underscore_pos == -1: return script
	
	var suffix = filename.substr(underscore_pos + 1).to_upper()
	if suffix.begins_with("R"): script["bright"] = true
	if suffix.begins_with("H"): script["hidden"] = true
	if suffix.begins_with("S"): script["stream"] = true
	if suffix.begins_with("N"): script["none_blend"] = true
	
	return script

static func _setup_base_material(mat: BaseMaterial3D, is_special: bool, is_shadow: bool,
		is_cutout: bool, texture_name_lower: String, is_chrome: bool = false) -> void:
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	
	if is_chrome:
		mat.metallic = 1.0
		mat.roughness = 0.05 # Even smoother for MU parity
		mat.specular = 1.0
		# MU Chrome is bright but shouldn't wash out (1.0 is enough with HDR sky)
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
	elif is_cutout:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		# PARITY: SVEN uses 128/255 (0.5) for alpha test, but for low-res textures 
		# this often causes artifacts. Using 0.1 is safer for visual parity.
		mat.alpha_scissor_threshold = 0.1 
	else:
		# Standard models
		mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
		# Many MU models use the vertex alpha for mesh blending even if textures are opaque
		# if is_special: mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA

static func _setup_shader_material(mat: ShaderMaterial, is_special: bool, is_shadow: bool,
		is_cutout: bool, _should_scroll: bool, is_wave: bool = false) -> void:
	# Note: Shader parameters must match mu_character.gdshader names
	mat.set_shader_parameter("use_wave", is_wave)
	
	if is_special:
		mat.set_shader_parameter("use_alpha_scissor", false)
		mat.set_shader_parameter("black_is_transparent", true) # Crucial for MU effects
		if is_shadow:
			mat.set_shader_parameter("is_additive", false)
		else:
			mat.set_shader_parameter("is_additive", true)
	elif is_cutout:
		mat.set_shader_parameter("use_alpha_scissor", true)
		mat.set_shader_parameter("alpha_test_threshold", 0.1)
		mat.set_shader_parameter("black_is_transparent", false)
	else:
		mat.set_shader_parameter("use_alpha_scissor", false)
		mat.set_shader_parameter("black_is_transparent", false)

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
		pass
