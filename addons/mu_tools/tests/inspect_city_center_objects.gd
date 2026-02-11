@tool
extends SceneTree

func _init():
	var api = load("res://addons/mu_tools/core/mu_api.gd").new()
	var data_api = api.data()
	
	# Load World 0 (Lorencia internally is often World 1 in files)
	var world_id = 0 # World 1 in MU files
	var data_path = "res://reference/MuMain/src/bin/Data"
	
	print("Inspecting Objects in Lorencia City Center...")
	var objects = data_api.load_world_data(world_id, data_path).get("objects", [])
	
	var city_rect = Rect2(64*100, 64*100, 128*100, 128*100) # MU Units
	
	var count = 0
	for obj in objects:
		var pos = obj.mu_pos_raw
		if city_rect.has_point(Vector2(pos.x, pos.z)):
			# Focus on specific types: 
			# Type 33: Archery Target/Dummy?
			# Type 1-10: Walls/Houses?
			if obj.type == 33 or obj.type < 20:
				print("Obj type %d at MU(%.0f, %.0f) Rot(%.1f, %.1f, %.1f)" % [
					obj.type, pos.x, pos.y, obj.mu_euler.x, obj.mu_euler.y, obj.mu_euler.z
				])
				count += 1
				if count > 50: break
				
	quit()
