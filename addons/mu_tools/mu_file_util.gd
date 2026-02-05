@tool
class_name MUFileUtil

## Utility for case-insensitive file access to suppress Godot warnings on macOS/Windows
## and ensure compatibility on Linux.

## Caches to avoid redundant disk I/O
static var _path_cache: Dictionary = {} # requested_path -> resolved_path
static var _dir_cache: Dictionary = {}  # dir_path -> { lowercase_name: actual_name }

static func resolve_case(path: String) -> String:
	if path.is_empty():
		return ""
		
	# Level 1: Full Path Cache (O(1))
	if _path_cache.has(path):
		return _path_cache[path]
		
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
		var dir_contents: Dictionary
		
		# Level 2: Directory Cache
		if _dir_cache.has(current_resolved):
			dir_contents = _dir_cache[current_resolved]
		else:
			dir_contents = {}
			var dir = DirAccess.open(current_resolved)
			if dir:
				dir.list_dir_begin()
				var fn = dir.get_next()
				while fn != "":
					dir_contents[fn.to_lower()] = fn
					fn = dir.get_next()
				dir.list_dir_end()
				_dir_cache[current_resolved] = dir_contents
			else:
				# Cannot even open the parent, fallback to original path for the rest
				_path_cache[path] = path
				return path 
			
		if dir_contents.has(target):
			current_resolved = current_resolved.path_join(dir_contents[target])
		else:
			# Segment not found with any casing, append original and stop resolving
			for j in range(i, segments.size()):
				current_resolved = current_resolved.path_join(segments[j])
			break
			
	_path_cache[path] = current_resolved
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
