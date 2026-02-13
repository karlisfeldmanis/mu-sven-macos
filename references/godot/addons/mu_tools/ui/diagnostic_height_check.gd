extends Node
# Quick diagnostic to capture object heights from running game

func _ready():
	var terrain = get_node("/root/Main/MUTerrain")
	if not terrain:
		print("ERROR: MUTerrain not found!")
		return
	
	await get_tree().create_timer(2.0).timeout
	
	print("\n======== OBJECT HEIGHT DIAGNOSTIC ========")
	print("Sampling first 30 spawned objects...")
	print("")
	print("%-25s %-15s %-12s %-12s" % ["Object Name", "Godot Pos", "Type", "Height Delta"])
	print("----------------------------------------------------------------------")
	
	var count = 0
	for child in terrain.get_children():
		if count >= 30:
			break
			
		if child is Node3D and child.name.contains("_"):
			var pos = child.position
			var terrain_h = terrain.get_height_at_world(pos)
			var delta = pos.y - terrain_h
			
			# Try to extract type from metadata
			var type_str = "?"
			var static_body = child.find_child("PickingBody")
			if static_body and static_body.has_meta("mu_pos"):
				var mu_pos = static_body.get_meta("mu_pos")
				type_str = "MU_Z:%.1f" % mu_pos.z
			
			print("%-25s (%.1f,%.1f,%.1f) %-12s %+.2f m" % [
				child.name.substr(0, 25),
				pos.x, pos.y, pos.z,
				type_str,
				delta
			])
			
			count += 1
	
	print("\n===========================================")
