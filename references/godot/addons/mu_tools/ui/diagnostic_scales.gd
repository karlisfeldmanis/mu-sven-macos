extends SceneTree

func _init():
	var MUTerrainParser = load("res://addons/mu_tools/parsers/terrain_parser.gd")
	var parser = MUTerrainParser.new()
	var MUFileUtil = load("res://addons/mu_tools/core/file_util.gd")
	var objects_path = MUFileUtil.resolve_case(
			"res://reference/MuMain/src/bin/Data/World1/EncTerrain1.obj")
	if not MUFileUtil.file_exists(objects_path):
		print("FATAL: File not found anywhere!")
		quit()
		return

	var objects = parser.parse_objects_file(objects_path)
	print("Found %d objects in file" % objects.size())
	
	print("\n--- Object Scale Diagnostic (World 1) ---")
	var grass_count = 0
	var scale_sum = 0.0
	var min_scale = 999.0
	var max_scale = -999.0
	
	for i in range(objects.size()):
		var obj = objects[i]
		if obj.type >= 20 and obj.type <= 27:
			grass_count += 1
			scale_sum += obj.scale
			min_scale = min(min_scale, obj.scale)
			max_scale = max(max_scale, obj.scale)
			if grass_count < 10:
				print("Grass Obj %d: Type=%d Scale=%.4f" % [i, obj.type, obj.scale])
	
	if grass_count > 0:
		print("\nSummary for Grass (Types 20-27):")
		print("  Count: %d" % grass_count)
		print("  Avg Scale: %.4f" % (scale_sum / grass_count))
		print("  Min Scale: %.4f" % min_scale)
		print("  Max Scale: %.4f" % max_scale)
	else:
		print("No grass objects found in World 1 Objects file!")
		
	# Check other potential IDs
	var total_types = {}
	var type_scales = {}
	for obj in objects:
		total_types[obj.type] = total_types.get(obj.type, 0) + 1
		if not type_scales.has(obj.type):
			type_scales[obj.type] = []
		type_scales[obj.type].append(obj.scale)
	
	print("\nObject Type Distribution (Sorted by Count):")
	var sorted_types = total_types.keys()
	sorted_types.sort_custom(func(a, b): return total_types[a] > total_types[b])
	
	for t in sorted_types:
		var count = total_types[t]
		var scales = type_scales[t]
		var avg_s = 0.0
		for s in scales: avg_s += s
		avg_s /= scales.size()
		# Only show types with > 20 instances or specifically grass IDs
		if count > 20 or (t >= 20 and t <= 27):
			print("  Type %d: %d instances (Avg Scale: %.4f)" % [t, count, avg_s])

	quit()
