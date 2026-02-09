extends SceneTree

const MUFileUtil = preload("res://addons/mu_tools/core/mu_file_util.gd")

## Pre-flight Check Script
## Run with: godot --path . --headless -s addons/mu_tools/preflight_check.gd

func _init() -> void:
	print("\n=== MU Remaster Pre-flight Check ===\n")
	
	var errors = 0
	var warnings = 0
	
	# 1. Check Essential Directories
	var dirs = ["res://addons/mu_tools/", "res://scenes/", "res://raw_data/Player/"]
	for d in dirs:
		if MUFileUtil.dir_exists(d):
			print("[✓] Directory exists: %s" % d)
		else:
			print("[!] MISSING Directory: %s" % d)
			errors += 1
			
	# 2. Check Essential Scripts (Parse Check)
	var scripts = [
		"res://addons/mu_tools/core/bmd_parser.gd",
		"res://addons/mu_tools/util/mu_animation_registry.gd",
		"res://addons/mu_tools/core/mu_state_machine.gd",
		"res://addons/mu_tools/core/animation_builder.gd",
		"res://addons/mu_tools/util/mu_logger.gd",
		"res://scenes/main.gd"
	]
	
	for s in scripts:
		if not MUFileUtil.file_exists(s):
			print("[!] MISSING Script: %s" % s)
			errors += 1
			continue
			
		var script_obj = load(s)
		if script_obj:
			print("[✓] Script parsed successfully: %s" % s)
		else:
			print("[!] FAILED to parse script: %s" % s)
			errors += 1

	# 3. Test BMD Parser
	print("\n--- Testing BMD Parser ---")
	var bmd_path = "res://raw_data/Player/player.bmd"
	if MUFileUtil.file_exists(bmd_path):
		var parser = load("res://addons/mu_tools/core/bmd_parser.gd").new()
		if parser.parse_file(bmd_path, false):
			print("[✓] BMD Parser successfully read player.bmd")
			print(
				"    Indices: %d mesh(es), %d bones, %d actions" % 
				[parser.meshes.size(), parser.bones.size(), parser.actions.size()]
			)
		else:
			print("[!] BMD Parser FAILED to read player.bmd")
			errors += 1
	else:
		print("[?] Skip: player.bmd not found for parser test")
		warnings += 1

	# 4. Test Animation Registry
	print("\n--- Testing Animation Registry ---")
	var registry = load("res://addons/mu_tools/util/mu_animation_registry.gd")
	if registry:
		var test_name = registry.get_action_name("player.bmd", 0)
		if test_name == "Set":
			print("[✓] Animation Registry working (0 -> Set)")
		else:
			print("[!] Animation Registry returned unexpected name: %s" % test_name)
			errors += 1

	# Final Summary
	print("\n=== Summary ===")
	print("Errors:   %d" % errors)
	print("Warnings: %d" % warnings)
	
	if errors > 0:
		print("\n[FAIL] Pre-flight found critical issues.")
		quit(1)
	else:
		print("\n[PASS] System is ready for flight.")
		quit(0)
