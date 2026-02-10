@tool
extends SceneTree

func _init():
	var data_api = load("res://addons/mu_tools/core/mu_data_api.gd").new()
	var data_path = "res://reference/MuMain/src/bin/Data"
	var world_id = 0
	var world_data = data_api.load_world_data(world_id, data_path)
	
	if world_data.is_empty():
		print("Failed to load world data")
		quit()
		return
		
	var map = world_data.mapping
	
	print("--- Road Alpha Diagnostic (World 1) ---")
	
	var center_x = 135
	var center_y = 135
	var radius = 5
	
	for y in range(center_y - radius, center_y + radius):
		var row_str = "Y=%3d | " % y
		for x in range(center_x - radius, center_x + radius):
			var idx = y * 256 + x
			var l1 = map.layer1[idx]
			var l2 = map.layer2[idx]
			var a = map.alpha[idx]
			# Format: [L1|L2:Alpha]
			row_str += "[%2d|%2d:%.3f] " % [l1, l2, a]
		print(row_str)
	print("------------------------------------------")
	quit()
