@tool
extends Node

class_name MUMaterialHelper

## Centralized utility for MU Online material effects.
## Bridges the gap between raw BMD textures and Godot material properties.

const MUModelRegistry = preload("res://addons/mu_tools/core/mu_model_registry.gd")

## Configures a material with MU-standard effects (Transparency, Blending, Scrolling).
## Works with both StandardMaterial3D (Viewer) and ShaderMaterial (Game).
static func setup_material(mat: Material, surface_index: int, model_path: String,
		texture_name: String = "", animated_materials: Array = []) -> void:
	if not mat: return
	
	var texture_name_lower = texture_name.to_lower()
	var mesh_name_lower = mat.resource_name.to_lower()
	
	# 1. Resolve Metadata from Registry
	var meta = MUModelRegistry.get_metadata(model_path)
	var scroll_uv = meta.get("scroll_uv", [])
	var scroll_index = int(meta.get("scroll_uv_index", -1))
	
	# 2. Check for UV scrolling
	var should_scroll = false
	if scroll_index != -1:
		if surface_index == scroll_index:
			should_scroll = true
	elif "water" in mesh_name_lower or "spout" in mesh_name_lower:
		should_scroll = true
	
	# 3. Effect Detection (Heuristics)
	var is_effect = false
	for kw in MUModelRegistry.EFFECT_KEYWORDS:
		if kw in texture_name_lower:
			is_effect = true
			break
	
	var is_shadow = false
	for kw in MUModelRegistry.SHADOW_KEYWORDS:
		if kw in texture_name_lower:
			is_shadow = true
			break
			
	var is_cutout = false
	for kw in MUModelRegistry.CUTOUT_KEYWORDS:
		if kw in texture_name_lower:
			is_cutout = true
			break
			
	var is_ignored = false
	for kw in MUModelRegistry.IGNORE_ALPHA_KEYWORDS:
		if kw in texture_name_lower:
			is_ignored = true
			break
	
	# scrolling or effect means special
	var is_special = (is_effect or is_shadow or should_scroll) and not is_ignored
	
	# 4. Apply Properties
	if mat is BaseMaterial3D:
		_setup_base_material(mat, is_special, is_shadow, is_cutout, should_scroll)
	elif mat is ShaderMaterial:
		_setup_shader_material(mat, is_special, is_shadow, is_cutout, should_scroll)
		
	# 5. Handle UV Animation Registration
	if should_scroll and not scroll_uv.is_empty() and scroll_uv.size() == 2:
		animated_materials.append({
			"material": mat,
			"speed": Vector2(scroll_uv[0], scroll_uv[1])
		})
	
	# 6. Apply Manual Tweaks from Registry
	var mat_tweak = MUModelRegistry.get_material_tweak(texture_name_lower)
	if not mat_tweak.is_empty():
		_apply_manual_tweaks(mat, mat_tweak)

static func _setup_base_material(mat: BaseMaterial3D, is_special: bool, is_shadow: bool,
		is_cutout: bool, _should_scroll: bool) -> void:
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	
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
		mat.alpha_scissor_threshold = 0.1
	else:
		# Standard models
		mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED

static func _setup_shader_material(mat: ShaderMaterial, is_special: bool, is_shadow: bool,
		is_cutout: bool, _should_scroll: bool) -> void:
	# Note: Shader parameters must match mu_character.gdshader names
	if is_special:
		mat.set_shader_parameter("use_alpha_scissor", false)
		if is_shadow:
			mat.set_shader_parameter("is_additive", false)
		else:
			mat.set_shader_parameter("is_additive", true)
	elif is_cutout:
		mat.set_shader_parameter("use_alpha_scissor", true)
		mat.set_shader_parameter("alpha_test_threshold", 0.1)
		mat.set_shader_parameter("use_wind", true) # Autocutout usually means plants
	else:
		mat.set_shader_parameter("use_alpha_scissor", false)

static func _apply_manual_tweaks(mat: Material, tweak: Dictionary) -> void:
	if mat is BaseMaterial3D:
		if tweak.has("transparency"): mat.transparency = tweak["transparency"]
		if tweak.has("blend_mode"): mat.blend_mode = tweak["blend_mode"]
		if tweak.has("cull_mode"): mat.cull_mode = tweak["cull_mode"]
		if tweak.has("alpha_scissor_threshold"): mat.alpha_scissor_threshold = tweak["alpha_scissor_threshold"]
	elif mat is ShaderMaterial:
		# Map tweaks to shader params if needed
		pass
