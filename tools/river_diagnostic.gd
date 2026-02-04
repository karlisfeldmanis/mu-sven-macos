@tool
extends SceneTree
const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")

func _init():
	var parser = MUTerrainParser.new()
	var path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
	var map_data = parser.parse_mapping_file(path)
	
	if not map_data:
		print("Failed to load map data")
		quit()
		return

	# Index Frequency Analysis
	var freq = {}
	for i in range(256*256):
		var val = map_data.layer1[i]
		freq[val] = freq.get(val, 0) + 1
	
	var sorted_indices = freq.keys()
	sorted_indices.sort_custom(func(a, b): return freq[a] > freq[b])
	
	print("\n=== Layer1 Index Frequency ===")
	for idx in sorted_indices:
		print("Index %d: %d tiles" % [idx, freq[idx]])

	# Analyze index 3 (Potential River?)
	var i3_min_x = 256
	var i3_max_x = 0
	var i3_min_y = 256
	var i3_max_y = 0
	var i3_count = 0
	
	for y in range(256):
		for x in range(256):
			var idx = y * 256 + x
			if map_data.layer1[idx] == 3:
				i3_min_x = min(i3_min_x, x)
				i3_max_x = max(i3_max_x, x)
				i3_min_y = min(i3_min_y, y)
				i3_max_y = max(i3_max_y, y)
				i3_count += 1
				
	print("\n=== Index 3 Diagnostic ===")
	print("Count: ", i3_count)
	if i3_count > 0:
		print("Bounds: X(%d to %d), Y(%d to %d)" % [i3_min_x, i3_max_x, i3_min_y, i3_max_y])
		
		print("\nSample Index 3 points (x, y):")
		var samples = 0
		for y in range(i3_min_y, i3_max_y + 1, (i3_max_y - i3_min_y) / 10 if i3_max_y > i3_min_y else 1):
			for x in range(i3_min_x, i3_max_x + 1):
				var idx = y * 256 + x
				if map_data.layer1[idx] == 3:
					print("(%d, %d)" % [x, y])
					samples += 1
					break

	
	# Analyze Grass (Index 0-2)
	var grass_count = 0
	for i in range(256*256):
		if map_data.layer1[i] >= 0 and map_data.layer1[i] <= 2:
			grass_count += 1
	print("\nGrass Tiles: ", grass_count)

	quit()
