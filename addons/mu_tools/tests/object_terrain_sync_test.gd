@tool
extends SceneTree

const MUAPI = preload("res://addons/mu_tools/core/mu_api.gd")

func _init():
	print("\n======================================================================")
	print("OBJECT-TERRAIN SYNC TEST")
	print("======================================================================")
	
	var api = MUAPI.new()
	var world_id = 0 # Lorencia
	var data = api.data().load_world_data(world_id, "res://reference/MuMain/src/bin/Data")
	
	if data.is_empty():
		print("Failed to load world data")
		quit()
		return

	var walls = []
	for obj in data.objects:
		# Fence/Wall types in Lorencia (see registry)
		if obj.type in [81, 82, 83, 84, 69, 70, 71, 72, 73, 74]:
			walls.append(obj)
			if walls.size() >= 10: break

	print("\nChecking first 10 walls in MU units:")
	for w in walls:
		# Raw MU Tile
		var tx = int(w.mu_pos_raw.x / 100.0)
		var ty = int(w.mu_pos_raw.y / 100.0)
		
		# Sample Terrain
		var h_idx = ty * 256 + tx
		if h_idx >= 0 and h_idx < data.mapping.alpha.size():
			var alpha = data.mapping.alpha[h_idx]
			var l1 = data.mapping.layer1[h_idx]
			
			print("Wall Type %d at MU(%d,%d) -> Terrain: Alpha=%.2f, Layer1=%d" % [
				w.type, tx, ty, alpha, l1
			])
		else:
			print("Wall Type %d at MU(%d,%d) -> OUT OF BOUNDS" % [w.type, tx, ty])
		
	print("\nSync Analysis:")
	print("- If Alpha is low and Layer1 is 0 or 1, it's on Grass.")
	print("- these walls should be AROUND grass area (Alpha=0, L1=0/1).")
	
	quit()
