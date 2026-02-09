extends SceneTree

const MU_TERRAIN_PARSER_CLASS = preload("res://addons/mu_tools/core/mu_terrain_parser.gd")

func _init():
	var parser = MU_TERRAIN_PARSER_CLASS.new()
	var objects_path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.obj"
	var objects = parser.parse_objects_file(objects_path)
	
	if objects.is_empty():
		quit()
		return

	print("Extreme Objects:")
	var min_x = 999; var max_x = -999; var min_y = 999; var max_y = -999;
	
	for obj in objects:
		var pos = obj.position
		var mu_x = pos.x
		var mu_y = pos.z + 255.0
		if mu_x < min_x: min_x = mu_x
		if mu_x > max_x: max_x = mu_x
		if mu_y < min_y: min_y = mu_y
		if mu_y > max_y: max_y = mu_y
	
	print("X Range: %.2f to %.2f" % [min_x, max_x])
	print("Y Range: %.2f to %.2f" % [min_y, max_y])
	
	print("\nObjects near (0,0) MU:")
	for obj in objects:
		var pos = obj.position
		var mu_x = pos.x; var mu_y = pos.z + 255.0
		if mu_x < 10 and mu_y < 10:
			print("Type %d at (%.2f, %.2f, %.2f) MU:(%.2f, %.2f)" % [obj.type, pos.x, pos.y, pos.z, mu_x, mu_y])

	print("\nObjects near (255,255) MU:")
	for obj in objects:
		var pos = obj.position
		var mu_x = pos.x; var mu_y = pos.z + 255.0
		if mu_x > 245 and mu_y > 245:
			print("Type %d at (%.2f, %.2f, %.2f) MU:(%.2f, %.2f)" % [obj.type, pos.x, pos.y, pos.z, mu_x, mu_y])

	quit()
