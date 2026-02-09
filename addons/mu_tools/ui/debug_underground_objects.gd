extends SceneTree

const MU_TERRAIN_PARSER_CLASS = preload("res://addons/mu_tools/core/mu_terrain_parser.gd")
const TERRAIN_SIZE = 256

const MUCoordinateUtils = preload("res://addons/mu_tools/core/mu_coordinate_utils.gd")

func _init():
	var parser = MU_TERRAIN_PARSER_CLASS.new()
	var objects_path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.obj"
	var objects = parser.parse_objects_file(objects_path)
	var heights_path = "res://reference/MuMain/src/bin/Data/World1/TerrainHeight.OZB"
	var heights = parser.parse_height_file(ProjectSettings.globalize_path(heights_path))
	
	if objects.is_empty() or heights.is_empty(): quit(); return

	print("Checking %d objects..." % objects.size())

	for i in range(objects.size()):
		var obj = objects[i]
		var pos = obj.position
		
		# Use Utility for consistent mapping
		var tile_coords = MUCoordinateUtils.world_to_tile(pos)
		var tile_x = tile_coords.x
		var tile_y = tile_coords.y
		
		# Validate bounds
		if MUCoordinateUtils.is_valid_tile(tile_x, tile_y):
			var idx = MUCoordinateUtils.tile_to_index(tile_x, tile_y)
			var h = heights[idx]
			var diff = pos.y - h
			
			# Check if object is significantly below terrain
			# Tolerance: -0.1m (approx 10cm) allow slop, warn if deeper
			if diff < -0.1:
				print("Obj %d: Type %d at (%.2f, %.2f, %.2f) Tile:(%d, %d) UNDERGROUND by %.2fm (Terrain H: %.2f)" % [
					i, obj.type, pos.x, pos.y, pos.z, tile_x, tile_y, abs(diff), h
				])

	quit()
