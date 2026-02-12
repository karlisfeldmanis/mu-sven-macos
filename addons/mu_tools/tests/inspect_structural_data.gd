@tool
extends SceneTree

const MUAPI = preload("res://addons/mu_tools/core/mu_api.gd")

func _init():
	print("\n======================================================================")
	print("CITY CENTER WALL INSPECTION")
	print("======================================================================")
	
	var api = MUAPI.new()
	var world_id = 0 # Lorencia
	var data = api.data().load_world_data(world_id, "res://reference/MuMain/src/bin/Data")
	
	if data.is_empty():
		print("Failed to load world data")
		quit()
		return

	print("\nWalls near MU(120, 128) [Bottom Wall]:")
	for obj in data.objects:
		var tx = int(obj.mu_pos_raw.x / 100.0)
		var ty = int(obj.mu_pos_raw.y / 100.0)
		
		if abs(tx - 120) < 5 and abs(ty - 128) < 10:
			const Registry = preload("res://addons/mu_tools/core/registry.gd")
			var model_path = Registry.get_object_path(obj.type, world_id)
			if "Wall" in model_path:
				var model_name = model_path.get_file().get_basename()
				print("Type: %d (%s), MU_Pos: (%d,%d), MU_Rot_Euler: %s" % [
					obj.type, model_name, tx, ty, obj.mu_euler
				])
	
	quit()
