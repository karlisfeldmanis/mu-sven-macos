
extends SceneTree

func _init():
	var parser = preload("res://addons/mu_tools/core/mu_terrain_parser.gd").new()
	var path = "res://reference/MuMain/src/bin/Data/World2/EncTerrain2.map"
	# Adjust path to be absolute or relative to project root safely
	path = ProjectSettings.globalize_path(path)
	
	print("Parsing: ", path)
	var map_data = parser.parse_mapping_file(path)
	
	if map_data:
		var max_val = 0
		var min_val = 255
		var count_over_31 = 0
		
		for val in map_data.layer1:
			if val > max_val: max_val = val
			if val < min_val: min_val = val
			if val > 31: count_over_31 += 1
			
		print("Layer 1 Stats:")
		print("  Min: ", min_val)
		print("  Max: ", max_val)
		print("  Values > 31: ", count_over_31)
		
		if count_over_31 > 0:
			print("  CONFIRMED: Data contains packed rotation/orientation bits!")
		else:
			print("  NOTE: No packed rotation bits found in Layer 1 (Standard Lorencia may normally be simple).")
			
	quit()
