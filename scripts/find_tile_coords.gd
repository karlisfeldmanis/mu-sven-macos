extends SceneTree

const MUAPI_CLASS = preload("res://addons/mu_tools/core/mu_api.gd")

func _init():
	var api = MUAPI_CLASS.new()
	var data_path = "res://reference/MuMain/src/bin/Data"
	var world_id = 0 # Lorencia is World 1, but API uses 0-based index? Wait, MUHeightmap uses world_id=1 for Lorencia?
	# main.gd default world_id is 0. 
	# MUHeightmap.gd: var world_dir = "World" + str(world_id + 1)
	# So world_id=0 -> World1.
	
	print("[DIAG] Loading World 1 (Lorencia) Mapping Data...")
	var world_data = api.data().load_world_data(0, data_path)
	if world_data.is_empty():
		printerr("Failed to load map data.")
		quit(1)
		return
		
	var map = world_data.mapping
	
	var all_indices = {}
	for val in map.layer1:
		all_indices[val] = all_indices.get(val, 0) + 1
	print("\n--- Unique Indices in Layer 1 ---")
	var sorted_keys = all_indices.keys()
	sorted_keys.sort()
	for k in sorted_keys:
		print("  Index %d: %d tiles" % [k, all_indices[k]])
		
	var targets = {
		"Ground Floors (Index 2-4)": [2, 3, 4],
		"Rock Floors (Index 7-13)": [7, 8, 9, 10, 11, 12, 13],
	}
	
	# Find a few coords for each
	for label in targets:
		print("\n--- ", label, " ---")
		var found = 0
		for y in range(256):
			for x in range(256):
				var idx = y * 256 + x
				if map.layer1[idx] in targets[label]:
					var sym = world_data.attributes.symmetry[idx]
					print("  Tile at MU(%d, %d) - Index %d, Sym %d" % [x, y, map.layer1[idx], sym])
					found += 1
					if found >= 20: break
			if found >= 20: break

	# Analyze Tavern Area (approx 120, 120 to 140, 140)
	print("\n--- Tavern Area Analysis (MU 120,120 to 140,140) ---")
	var tavern_indices = {}
	for y in range(120, 140):
		for x in range(120, 140):
			var idx = y * 256 + x
			var t_idx = map.layer1[idx]
			tavern_indices[t_idx] = tavern_indices.get(t_idx, 0) + 1
			
	print(" Predominant indices: ", tavern_indices)
	# 3 = TileGround02
	# 4 = TileGround03
	
	quit(0)
