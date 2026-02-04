@tool
extends SceneTree

func _init():
	print("\n============================================================")
	print("MU ONLINE REMASTER - LAUNCH DIAGNOSTIC")
	print("============================================================\n")
	
	var critical_resources = [
		"res://addons/mu_tools/mu_terrain.gd",
		"res://addons/mu_tools/mu_terrain_parser.gd",
		"res://addons/mu_tools/coordinate_utils.gd",
		"res://addons/mu_tools/mu_texture_loader.gd",
		"res://addons/mu_tools/mesh_builder.gd",
		"res://addons/mu_tools/mu_preflight.gd",
		"res://core/shaders/mu_terrain.gdshader",
		"res://core/shaders/mu_character.gdshader",
		"res://scenes/lorencia_effects/mu_fire.gd",
		"res://scenes/lorencia_effects/shaders/mu_fire.gdshader",
		"res://scenes/lorencia_effects/shaders/leaf_flutter.gdshader",
		"res://scenes/lorencia_effects/falling_leaves.gd"
	]
	
	var all_ok = true
	for res_path in critical_resources:
		print("Checking: %s" % res_path)
		if not FileAccess.file_exists(res_path):
			print("  [ERROR] File DOES NOT EXIST on disk.")
			all_ok = false
			continue
			
		var res = load(res_path)
		if res == null:
			print("  [ERROR] FAILED TO LOAD/PARSE (Syntax error or circular dependency?).")
			all_ok = false
		else:
			print("  [OK] Loaded successfully.")
			
	if all_ok:
		print("\n============================================================")
		print("ALL CRITICAL RESOURCES OK. Project should launch.")
		print("============================================================\n")
	else:
		print("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
		print("DIAGNOSTIC FOUND ERRORS. FIX BEFORE LAUNCHING.")
		print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")
		
	quit(0 if all_ok else 1)
