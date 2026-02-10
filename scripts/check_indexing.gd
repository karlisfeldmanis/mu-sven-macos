extends SceneTree

const MUAPI_CLASS = preload("res://addons/mu_tools/core/mu_api.gd")

func _init():
	var api = MUAPI_CLASS.new()
	var data_path = "res://reference/MuMain/src/bin/Data"
	
	print("[DIAG] Analyzing Lorencia Attribute Map (World 0)...")
	var world_data = api.data().load_world_data(0, data_path)
	if world_data.is_empty():
		quit(1)
		return
		
	var att = world_data.attributes
	
	print("\n--- Safe Zone Layout Check (0x01) ---")
	print("Sampling 16x16 blocks to see the shape...")
	
	# Mapping A: idx = y * 256 + x (Row-Major)
	# Mapping B: idx = x * 256 + y (Column-Major)
	
	for by in range(0, 256, 16):
		var line = ""
		for bx in range(0, 256, 16):
			var idx = by * 256 + bx
			var val = att[idx]
			if val & 0x01: line += "#"
			else: line += "."
		print(line)
		
	print("\nIf the '#' form a central structure, Row-Major is correct.")
	
	quit(0)
