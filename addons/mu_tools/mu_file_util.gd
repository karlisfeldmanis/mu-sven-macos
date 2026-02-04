@tool
class_name MUFileUtil

## Utility for case-insensitive file access to suppress Godot warnings on macOS/Windows
## and ensure compatibility on Linux.

static func resolve_case(path: String) -> String:
	if path.is_empty():
		return ""
		
	var is_res = path.begins_with("res://")
	var is_user = path.begins_with("user://")
	
	# We need to resolve each segment of the path from the root
	# because DirAccess.open() also expects correct casing for directories.
	var segments = []
	var root = ""
	
	if is_res:
		root = "res://"
		segments = path.trim_prefix("res://").split("/", false)
	elif is_user:
		root = "user://"
		segments = path.trim_prefix("user://").split("/", false)
	else:
		# Global path - start from root according to OS
		if OS.get_name() == "Windows":
			# Handle C:/...
			var drive_split = path.split(":", false, 1)
			if drive_split.size() > 1:
				root = drive_split[0] + ":/"
				segments = drive_split[1].split("/", false)
			else:
				root = "/"
				segments = path.split("/", false)
		else:
			root = "/"
			segments = path.split("/", false)

	var current_resolved = root
	for i in range(segments.size()):
		var target = segments[i].to_lower()
		var dir = DirAccess.open(current_resolved)
		if not dir:
			# Cannot even open the parent, fallback to original path for the rest
			return path 
			
		dir.list_dir_begin()
		var found = false
		var fn = dir.get_next()
		while fn != "":
			if fn.to_lower() == target:
				current_resolved = current_resolved.path_join(fn)
				found = true
				break
			fn = dir.get_next()
		dir.list_dir_end()
		
		if not found:
			# Segment not found with any casing, append original and stop resolving
			for j in range(i, segments.size()):
				current_resolved = current_resolved.path_join(segments[j])
			break
			
	return current_resolved

static func open_file(path: String, mode: FileAccess.ModeFlags) -> FileAccess:
	var resolved = resolve_case(path)
	return FileAccess.open(resolved, mode)

static func file_exists(path: String) -> bool:
	var resolved = resolve_case(path)
	return FileAccess.file_exists(resolved)

static func dir_exists(path: String) -> bool:
	var resolved = resolve_case(path)
	return DirAccess.dir_exists_absolute(resolved)
