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
		var dir = DirAccess.open(base_dir)
		var files = []
		if dir:
			dir.list_dir_begin()
			var fn = dir.get_next()
			while fn != "":
				files.append(fn)
				fn = dir.get_next()
		print("  [TextureResolver] FAILED to find: '%s' for internal '%s' in %s" % 
				[search_name, internal_name, base_dir])
		print("    Files in dir: ", files)
	return result

## Searches for a texture in the BMD directory and common subfolders (Case-Insensitive)
static func _search_texture(base_dir: String, filename: String) -> String:
	# 1. Proactively resolve case using our robust utility.
	# This handles both directory and file case mismatches from the root.
	var full_path = base_dir.path_join(filename)
	var resolved = MUFileUtil.resolve_case(full_path)
	
	if MUFileUtil.file_exists(resolved):
		return resolved
			
	# 2. Check common alternative locations
	if base_dir.contains("/Object"):
		# Try looking in sister Object folders or base Data folder
		var data_dir = base_dir.get_base_dir()
		var alt_path = data_dir.path_join(filename)
		var resolved_alt = MUFileUtil.resolve_case(alt_path)
		if MUFileUtil.file_exists(resolved_alt): 
			return resolved_alt
		
	# 3. Check "Data" root if we can find it
	if base_dir.contains("/raw_data/"):
		var data_root = base_dir.split("/raw_data/")[0] + "/raw_data/"
		if data_root != base_dir:
			var res = _search_texture(data_root, filename)
			if not res.is_empty(): return res
			
	return ""
