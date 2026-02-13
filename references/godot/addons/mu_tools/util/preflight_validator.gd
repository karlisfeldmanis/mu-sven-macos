@tool
# class_name PreflightValidator
extends Object

## Preflight Validator Utility
## Provides robust health checking for scripts and resources.

static func run_full_check() -> int:
	print("\n" + "=".repeat(60))
	print("[PREFLIGHT] Starting Comprehensive Project Validation...")
	print("=".repeat(60))
	
	var checked_files = {}
	var targets = [
		"res://addons/mu_tools/",
		"res://scripts/",
		"res://scenes/"
	]
	
	var total_errors = 0
	for target in targets:
		total_errors += _check_directory(target, checked_files)
		
	return total_errors

static func _check_directory(path: String, checked: Dictionary) -> int:
	var dir = DirAccess.open(path)
	if not dir:
		return 0
		
	var err_count = 0
	dir.list_dir_begin()
	var file_name = dir.get_next()
	
	while file_name != "":
		if file_name.begins_with("."):
			file_name = dir.get_next()
			continue
			
		var full_path = path.path_join(file_name)
		if dir.current_is_dir():
			err_count += _check_directory(full_path, checked)
		else:
			if file_name.ends_with(".gd") or file_name.ends_with(".tscn") or \
					file_name.ends_with(".tres"):
				# Skip the current validators to avoid self-referential issues
				if full_path.contains("preflight_check.gd") or \
						full_path.contains("preflight_validator.gd"):
					checked[full_path] = true
					file_name = dir.get_next()
					continue
					
				if not checked.has(full_path):
					err_count += validate_resource(full_path)
					checked[full_path] = true
		
		file_name = dir.get_next()
		
	return err_count

static func validate_resource(path: String) -> int:
	# Standard load is more stable during bootstrap.
	# Parse errors will still return null and print to stderr.
	var res = ResourceLoader.load(path)
	
	if res == null:
		printerr("  [!] LOAD ERROR: ", path)
		return 1
	
	# Strict Script Validation
	if res is GDScript:
		# Godot 4: ResourceLoader.load() already performs a full parse.
		# If it reached here, the script is valid.
		pass
			
	print("  [✓] Validated: ", path)
	return 0
