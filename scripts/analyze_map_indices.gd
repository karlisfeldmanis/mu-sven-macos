@tool
extends SceneTree

func _init():
	var parser = preload("res://addons/mu_tools/core/mu_terrain_parser.gd").new()
	var path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
	path = ProjectSettings.globalize_path(path)
	
	print("\n[ANALYSIS] Checking Texture Indices in: ", path)
	var map_data = parser.parse_mapping_file(path)
	
	if not map_data:
		print("[ERROR] Failed to parse mapping file.")
		quit(1)
		return
		
	var unique_l1 = {}
	var unique_l2 = {}
	
	for val in map_data.layer1:
		unique_l1[val] = unique_l1.get(val, 0) + 1
		
	for val in map_data.layer2:
		if val != 255: # 255 usually means no texture
			unique_l2[val] = unique_l2.get(val, 0) + 1
			
	print("\nLayer 1 Unique Indices:")
	var l1_keys = unique_l1.keys()
	l1_keys.sort()
	for k in l1_keys:
		print("  Index %3d: %d tiles" % [k, unique_l1[k]])
		
	print("\nLayer 2 Unique Indices:")
	var l2_keys = unique_l2.keys()
	l2_keys.sort()
	for k in l2_keys:
		print("  Index %3d: %d tiles" % [k, unique_l2[k]])
		
	quit(0)
