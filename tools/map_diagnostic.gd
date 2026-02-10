extends SceneTree

const MUTerrainParser = preload("res://addons/mu_tools/core/mu_terrain_parser.gd")

func _init():
	var parser = MUTerrainParser.new()
	var path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
	
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		print("FAILED to open map file")
		quit()
		return
		
	var encrypted_data = file.get_buffer(file.get_length())
	var data = MUTerrainParser.decrypt_map_file(encrypted_data)
	
	print("\n=== MAP DIAGNOSTIC ===")
	print("File: ", path)
	print("Size: ", data.size())
	print("Header (first 16 bytes): ", data.slice(0, 16))
	
	# Map Border: What is the dominant index?
	var counts = {}
	for i in range(2, data.size() / 3):
		var val = data[i]
		counts[val] = counts.get(val, 0) + 1
	
	var sorted_indices = counts.keys()
	sorted_indices.sort_custom(func(a, b): return counts[a] > counts[b])
	
	print("\nDominant Indices in Layer1 (Top 10):")
	for i in range(min(10, sorted_indices.size())):
		var idx = sorted_indices[i]
		print("  Index %d: %d tiles" % [idx, counts[idx]])
	
	# Check the "River Exit" area (Bottom-Right corner)
	# 256x256 map. Bottom-Right is roughly (240-255, 0-20)? 
	# Or depends on Forward/Right. Let's check both possibilities.
	print("\nRiver Area Sample (Bottom-Right 16x16):")
	for row in range(240, 256):
		var line = ""
		for col in range(240, 256):
			var idx = 2 + (row * 256 + col)
			line += "%d " % data[idx]
		print(line)
	
	quit()
