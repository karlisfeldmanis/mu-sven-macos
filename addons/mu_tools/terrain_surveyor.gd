extends SceneTree

const MU_TERRAIN_PARSER_CLASS = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/coordinate_utils.gd")
const TERRAIN_SIZE = 256

func _init():
	print("[Surveyor] Starting terrain analysis...")
	var parser = MU_TERRAIN_PARSER_CLASS.new()
	var objects_path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.obj"
	var objects = parser.parse_objects_file(objects_path)
	var heights_path = "res://reference/MuMain/src/bin/Data/World1/TerrainHeight.OZB"
	var heights = parser.parse_height_file(ProjectSettings.globalize_path(heights_path))
	
	if objects.is_empty() or heights.is_empty(): quit(); return

	var normal_errors = 0
	var normal_accum_error = 0.0
	var flipped_errors = 0
	var flipped_accum_error = 0.0
	
	print("[Surveyor] Analyzing %d objects..." % objects.size())
	
	for i in range(objects.size()):
		var obj = objects[i]
		var pos = obj.position
		
		# Current Logic (Normal)
		var tile = MUCoordinateUtils.world_to_tile(pos)
		
		# Flipped Logic (Invert Y)
		# Should this be (tile.x, 255 - tile.y)?
		var tile_y_flipped = 255 - tile.y
		
		var h_normal = get_height(heights, tile.x, tile.y)
		var h_flipped = get_height(heights, tile.x, tile_y_flipped)
		
		var delta_normal = abs(pos.y - h_normal)
		var delta_flipped = abs(pos.y - h_flipped)
		
		if delta_normal > 0.5: normal_errors += 1
		normal_accum_error += delta_normal
		
		if delta_flipped > 0.5: flipped_errors += 1
		flipped_accum_error += delta_flipped
		
		# Sample output for significant differences
		if i % 200 == 0:
			print("Obj %d (%.1f, %.1f, %.1f) Tile(%d, %d): Normal H=%.2f (d=%.2f) | Flipped H=%.2f (d=%.2f)" % [
				i, pos.x, pos.y, pos.z, tile.x, tile.y, h_normal, delta_normal, h_flipped, delta_flipped
			])

	print("\n[Surveyor] Results:")
	print("  Normal Logic:  %d significant errors (>0.5m), Mean Error: %.4f" % [normal_errors, normal_accum_error / objects.size()])
	print("  Flipped Logic: %d significant errors (>0.5m), Mean Error: %.4f" % [flipped_errors, flipped_accum_error / objects.size()])
	
	if flipped_accum_error < normal_accum_error:
		print("  CONCLUSION: Flipped logic (Y -> 255-Y) seems BETTER.")
	else:
		print("  CONCLUSION: Normal logic seems BETTER.")
		
	quit()

func get_height(heights: PackedFloat32Array, x: int, y: int) -> float:
	if x < 0 or x >= TERRAIN_SIZE or y < 0 or y >= TERRAIN_SIZE:
		return 0.0
	return heights[y * TERRAIN_SIZE + x]
