extends Node

# Diagnostic: Find underground objects and their raw MU Z values

func _ready():
	await get_tree().create_timer(2.0).timeout
	
	var terrain = get_node("/root/Main/MUTerrain")
	if not terrain:
		print("ERROR: Terrain not found")
		return
	
	print("\n========== UNDERGROUND OBJECT DIAGNOSTIC ==========")
	
	var underground_count = 0
	var correct_count = 0
	var floating_count = 0
	
	for child in terrain.get_children():
		if not child is Node3D:
			continue
			
		var pos = child.position
		var terrain_h = terrain.get_height_at_world(pos)
		var delta = pos.y - terrain_h
		
		# Get raw MU position if available
		var mu_z = "N/A"
		var static_body = child.find_child("PickingBody")
		if static_body and static_body.has_meta("mu_pos"):
			var mu_pos = static_body.get_meta("mu_pos")
			mu_z = "%.1f" % mu_pos.z
		
		# Categorize
		if delta < -0.5:  # More than 50cm underground
			underground_count += 1
			if underground_count <= 20:  # Show first 20
				print("UNDERGROUND: %s | Y=%.2f TerrainH=%.2f Delta=%.2f | MU_Z=%s" % [
					child.name, pos.y, terrain_h, delta, mu_z
				])
		elif delta > 0.5:  # More than 50cm floating
			floating_count += 1
			if floating_count <= 20:
				print("FLOATING:    %s | Y=%.2f TerrainH=%.2f Delta=%.2f | MU_Z=%s" % [
					child.name, pos.y, terrain_h, delta, mu_z
				])
		else:
			correct_count += 1
	
	print("\n========== SUMMARY ==========")
	print("Underground (< -0.5m): %d" % underground_count)
	print("Floating (> +0.5m):    %d" % floating_count)
	print("Correct (-0.5 to +0.5): %d" % correct_count)
	print("Total objects: %d" % (underground_count + floating_count + correct_count))
	print("=====================================\n")
