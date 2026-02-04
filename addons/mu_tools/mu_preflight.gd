@tool
class_name MUPreflight
extends RefCounted

## MU Online Map Pre-flight Diagnostic Tool
##
## Validates all required assets for a world before loading.
## Checks for terrain files, BMD models, and texture resolution.

const MUTextureResolver = preload("res://addons/mu_tools/mu_texture_resolver.gd")
const BMDParserResource = preload("res://addons/mu_tools/bmd_parser.gd")
const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")
const MUFileUtil = preload("res://addons/mu_tools/mu_file_util.gd")

class PreflightReport:
	var success: bool = true
	var errors: Array[String] = []
	var warnings: Array[String] = []
	var total_objects: int = 0
	var missing_bmds: int = 0
	var missing_textures: int = 0

static func validate_world(p_world_id: int, p_data_path: String, 
		p_object_map: Dictionary) -> Dictionary:
	var report = {
		"success": true,
		"errors": [],
		"warnings": [],
		"total_objects": 0,
		"missing_bmds": 0,
		"missing_textures": 0,
		"missing_files": []
	}
	
	MULogger.info("Starting validation for World %d..." % p_world_id)
	
	# 1. Check Base Terrain Files
	var base_files = [
		"TerrainHeight.OZB",
		"EncTerrain" + str(p_world_id) + ".map",
		"EncTerrain" + str(p_world_id) + ".obj",
		"EncTerrain" + str(p_world_id) + ".att",
		"TerrainLight.OZJ"
	]
	
	var world_dir = "World" + str(p_world_id)
	for file in base_files:
		var path = p_data_path.path_join(world_dir).path_join(file)
		if not MUFileUtil.file_exists(path):
			report.errors.append("Missing base terrain file: %s" % file)
			report.success = false
			report.missing_files.append(file)
			
	if not report.success:
		return report
		
	# 2. Parse Objects File
	var parser = MUTerrainParser.new()
	var obj_path = p_data_path.path_join(world_dir).path_join(
			"EncTerrain" + str(p_world_id) + ".obj")
	var object_data = parser.parse_objects_file(obj_path)
	report.total_objects = object_data.size()
	
	# 3. Validate World Objects
	var model_dir = p_data_path.path_join("Object" + str(p_world_id))
	var checked_bmds = {} # path -> bool
	
	for obj in object_data:
		var bmd_name = _get_bmd_name(obj.type, p_object_map)
		if bmd_name.is_empty():
			var warn = "No mapping for Object Type %d" % obj.type
			if not warn in report.warnings:
				report.warnings.append(warn)
			continue
			
		var bmd_path = model_dir.path_join(bmd_name + ".bmd")
		var bmd_abs = MUFileUtil.resolve_case(bmd_path)
		
		if not FileAccess.file_exists(bmd_abs):
			if not bmd_path in report.missing_files:
				report.missing_bmds += 1
				report.missing_files.append(bmd_path)
				report.errors.append("Missing BMD: %s" % bmd_path)
			continue
			
		# 4. In-depth BMD Texture Check
		if not bmd_abs in checked_bmds:
			var bmd_parser = BMDParserResource.new()
			if bmd_parser.parse_file(bmd_abs):
				for mesh in bmd_parser.meshes:
					if not mesh.texture_filename: continue
					var tex_path = MUTextureResolver.resolve_texture_path(bmd_abs, mesh.texture_filename)
					if tex_path.is_empty():
						report.missing_textures += 1
						var m_tex = "Missing Texture '%s' for BMD '%s'" % [
								mesh.texture_filename, bmd_name]
						report.warnings.append(m_tex)
			checked_bmds[bmd_abs] = true
			
	report.success = report.errors.is_empty()
	return report

static func _get_bmd_name(p_type: int, p_object_map: Dictionary) -> String:
	var base_type = -1
	if p_object_map.has(p_type):
		base_type = p_type
	else:
		var keys = p_object_map.keys()
		keys.sort()
		for k in keys:
			if k <= p_type:
				base_type = k
			else:
				break
				
	if base_type == -1: return ""
	
	var pattern = p_object_map[base_type]
	if "%02d" in pattern:
		var idx = (p_type - base_type) + 1
		return pattern % idx
	return pattern
