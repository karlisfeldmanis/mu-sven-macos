extends SceneTree

## MU Online Remaster - Preflight Check (Phase 5)
## Validates all project scripts and scenes to ensure no parse errors exist.

func _init():
	print("\n[PREFLIGHT] Starting project validation...")
	var errors = 0
	var total_checked = 0
	
	errors += _check_directory("res://addons/mu_tools/")
	errors += _check_directory("res://core/")
	errors += _check_directory("res://scripts/")
	errors += _check_directory("res://scenes/")
	
	if errors > 0:
		printerr("\n[PREFLIGHT] FAILED: Found ", errors, " validation errors.")
		quit(1)
	else:
		print("\n[PREFLIGHT] SUCCESS: All scripts and resources passed validation.")
		quit(0)

func _check_directory(path: String) -> int:
	var dir = DirAccess.open(path)
	if not dir:
		return 0
		
	var err_count = 0
	dir.list_dir_begin()
	var file_name = dir.get_next()
	
	while file_name != "":
		var full_path = path.path_join(file_name)
		if dir.current_is_dir():
			if not file_name.begins_with("."):
				err_count += _check_directory(full_path + "/")
		else:
			if file_name.ends_with(".gd") or file_name.ends_with(".tscn") or file_name.ends_with(".tres"):
				err_count += _validate_resource(full_path)
		
		file_name = dir.get_next()
		
	return err_count

func _validate_resource(path: String) -> int:
	# Attempt to load the resource. 
	# If there's a parse error, Godot will print it to stderr and load() will fail.
	var res = load(path)
	if res == null:
		printerr("  [!] Validation Failed: ", path)
		return 1
	else:
		print("  [✓] Validated: ", path)
		return 0
