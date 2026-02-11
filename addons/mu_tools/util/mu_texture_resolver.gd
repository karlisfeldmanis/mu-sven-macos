@tool
class_name MUTextureResolver
const MUFileUtil = preload("res://addons/mu_tools/core/mu_file_util.gd")

## Utility for resolving MU texture names to Godot paths (Phase 1)
##
## Converts internal BMD texture names (e.g. "Armor01.tga") to their 
## encrypted Godot-imported paths (e.g. "res://.../Armor01.ozt").

## Resolves a texture filename to a project path
static func resolve_texture_path(bmd_path: String, internal_name: String) -> String:
	if internal_name.is_empty():
		return ""
		
	var base_dir = bmd_path.get_base_dir()
	var raw_name = internal_name.strip_edges()
	var clean_name = raw_name.to_lower()
	
	# MU logic: 
	# .tga and .bmp -> resolve to .ozt
	# .jpg -> resolve to .ozj
	var target_ext = ""
	if clean_name.ends_with(".tga") or clean_name.ends_with(".bmp"):
		target_ext = ".ozt"
	elif clean_name.ends_with(".jpg"):
		target_ext = ".ozj"
	else:
		# If no extension or unknown, try both with original mapping
		var path_ozj = _search_texture(base_dir, raw_name.get_basename() + ".ozj")
		if not path_ozj.is_empty(): return path_ozj
		return _search_texture(base_dir, raw_name.get_basename() + ".ozt")

	var stem = raw_name.get_basename()
	var search_name = stem + target_ext
	var result = _search_texture(base_dir, search_name)
	
	if result.is_empty():
		# Try case-insensitive search for the RAW name if mapping failed
		result = _search_texture(base_dir, raw_name)
		
	if result.is_empty():
		push_warning("  [TextureResolver] FAILED to find: '%s' (internal '%s') in %s" % 
				[search_name, internal_name, base_dir])
	return result

## Searches for a texture in the BMD directory and common subfolders (Case-Insensitive)
static func _search_texture(base_dir: String, filename: String) -> String:
	# 1. Direct check in current directory
	var full_path = base_dir.path_join(filename)
	var resolved = MUFileUtil.resolve_case(full_path)
	if MUFileUtil.file_exists(resolved):
		return resolved
			
	# 2. Check common alternative locations
	# MU often places textures in "Item" folder even if model is in "ObjectX"
	if base_dir.contains("/Data/"):
		var data_root = base_dir.split("/Data/")[0] + "/Data/"
		var common_folders = ["Item", "Object1", "Object2", "Object3", "Player", "Texture"]
		
		for folder in common_folders:
			var alt_dir = data_root.path_join(folder)
			var alt_path = alt_dir.path_join(filename)
			var resolved_alt = MUFileUtil.resolve_case(alt_path)
			if MUFileUtil.file_exists(resolved_alt): 
				return resolved_alt
		
	# 3. Check for specific "Texture" subfolders within Objects
	if base_dir.contains("/Object"):
		var obj_dir = base_dir.split("/Object")[0] + "/Object"
		var alt_path = obj_dir.path_join("Texture").path_join(filename)
		var resolved_alt = MUFileUtil.resolve_case(alt_path)
		if MUFileUtil.file_exists(resolved_alt):
			return resolved_alt
			
	return ""
