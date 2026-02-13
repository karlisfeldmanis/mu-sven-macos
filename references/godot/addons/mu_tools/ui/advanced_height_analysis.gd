extends Node

# Advanced diagnostic: Analyze MU Z patterns for all objects

func _ready():
	await get_tree().create_timer(2.0).timeout
	
	var terrain = get_node("/root/Main/MUTerrain")
	if not terrain:
		print("ERROR: Terrain not found")
		return
	
	print("\n========== OBJECT HEIGHT ANALYSIS ==========")
	
	# Group objects by their MU Z range
	var z_groups = {
		"very_negative": [],  # Z < -400
		"near_minus_500": [], # -600 < Z < -400
		"near_zero": [],      # -100 < Z < 100
		"positive_low": [],   # 100 < Z < 1000
		"positive_high": []   # Z > 1000
	}
	
	var total_objects = 0
	
	for child in terrain.get_children():
		if not child is Node3D:
			continue
		
		var static_body = child.find_child("PickingBody")
		if not static_body or not static_body.has_meta("mu_pos"):
			continue
		
		var mu_pos = static_body.get_meta("mu_pos")
		var mu_z = mu_pos.z
		var godot_y = child.position.y
		var terrain_h = terrain.get_height_at_world(child.position)
		
		var delta = godot_y - terrain_h
		var obj_type = static_body.get_meta("object_type") if static_body.has_meta("object_type") else -1
		
		var data = {
			"name": child.name,
			"mu_z": mu_z,
			"godot_y": godot_y,
			"terrain_h": terrain_h,
			"delta": delta,
			"type": obj_type
		}
		
		# Categorize
		if mu_z < -400:
			z_groups["very_negative"].append(data)
		elif mu_z >= -600 and mu_z < -400:
			z_groups["near_minus_500"].append(data)
		elif mu_z >= -100 and mu_z < 100:
			z_groups["near_zero"].append(data)
		elif mu_z >= 100 and mu_z < 1000:
			z_groups["positive_low"].append(data)
		else:
			z_groups["positive_high"].append(data)
		
		total_objects += 1
	
	# Print analysis
	for group_name in z_groups:
		var group = z_groups[group_name]
		if group.is_empty():
			continue
		
		print("\n--- %s (%d objects) ---" % [group_name.to_upper(), group.size()])
		
		# Show first 5 examples
		for i in mini(5, group.size()):
			var obj = group[i]
			var status = "OK" if abs(obj.delta) < 0.5 else ("FLOAT" if obj.delta > 0.5 else "UNDER")
			print("  [%s] %s | MU_Z=%.1f GodotY=%.2f TerrainH=%.2f Δ=%.2f" % [
				status, obj.name.left(20), obj.mu_z, obj.godot_y, obj.terrain_h, obj.delta
			])
		
		# Calculate stats
		var correct_count = 0
		var floating_count = 0
		var underground_count = 0
		
		for obj in group:
			if abs(obj.delta) < 0.5:
				correct_count += 1
			elif obj.delta > 0.5:
				floating_count += 1
			else:
				underground_count += 1
		
		print("  Stats: %d OK, %d floating, %d underground" % [correct_count, floating_count, underground_count])
	
	print("\n========== SUMMARY ==========")
	print("Total objects analyzed: %d" % total_objects)
	print("=====================================\n")
