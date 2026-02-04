extends SceneTree

const MU_TERRAIN_PARSER_CLASS = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/coordinate_utils.gd")

func _init():
	print("[Analysis] Checking StoneWall01_2507...")
	
	# Target Data
	var godot_pos = Vector3(77.12973, -2.101703, -38.31984)
	
	# 1. Calculate Tile
	var tile = MUCoordinateUtils.world_to_tile(godot_pos)
	print("  Godot Pos: %s" % godot_pos)
	print("  Calculated Tile: (%d, %d)" % [tile.x, tile.y])
	
	# 2. Load Heights
	var parser = MU_TERRAIN_PARSER_CLASS.new()
	var heights_path = "res://reference/MuMain/src/bin/Data/World1/TerrainHeight.OZB"
	var heights = parser.parse_height_file(ProjectSettings.globalize_path(heights_path))
	
	if heights.is_empty():
		print("  Error: Could not load terrain heights.")
		quit()
		return
		
	# 3. Check Heights (Area Scan)
	var h_normal = get_height(heights, tile.x, tile.y) # Current Tile Height

	print("  Area Scan (5x5 around 216, 77):")
	var min_h = 999.0
	var max_h = -999.0
	
	for dy in range(-2, 3):
		for dx in range(-2, 3):
			var tx = tile.x + dx
			var ty = tile.y + dy
			var h = get_height(heights, tx, ty)
			min_h = min(min_h, h)
			max_h = max(max_h, h)
			
			if dx == 0 and dy == 0:
				print("    Center (%d, %d): %.4f (Diff: %.4f)" % [tx, ty, h, godot_pos.y - h])
	
	print("  Min H in area: %.4f" % min_h)
	print("  Max H in area: %.4f" % max_h)
	
	# Hypothesis Check
	if abs(abs(godot_pos.y) - h_normal) < 0.5:
		print("  HYPOTHESIS: Z might be unsigned/absolute? |%.4f| vs %.4f" % [godot_pos.y, h_normal])
	
	var relative_h = h_normal + godot_pos.y
	print("  Relative (Terrain + ObjY): %.4f" % relative_h)
	
	quit()

func get_height(heights: PackedFloat32Array, x: int, y: int) -> float:
	if x < 0 or x >= 256 or y < 0 or y >= 256:
		return 0.0
	return heights[y * 256 + x]
