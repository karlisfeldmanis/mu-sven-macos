extends SceneTree

## Test script to verify MUCoordinates conversion functions

const MUCoordinates = preload("res://addons/mu_tools/mu_coordinates.gd")

func _init():
	print("=== MUCoordinates Test ===\n")
	
	# Test 1: Tile to World (Center of Lorencia)
	print("Test 1: Tile to World Conversion")
	var center_tile = Vector2i(128, 128)
	var world_pos = MUCoordinates.tile_to_world(center_tile.x, center_tile.y, 0.0)
	print("  Tile (128, 128) -> World: ", world_pos)
	print("  Expected: Vector3(128.5, 0, 128.5)")
	assert(abs(world_pos.x - 128.5) < 0.001, "X should be 128.5")
	assert(abs(world_pos.z - 128.5) < 0.001, "Z should be 128.5")
	print("  ✓ PASS\n")
	
	# Test 2: World to Tile
	print("Test 2: World to Tile Conversion")
	var tile = MUCoordinates.world_to_tile(Vector3(128.5, 0, 128.5))
	print("  World (128.5, 0, 128.5) -> Tile: ", tile)
	print("  Expected: Vector2i(128, 128)")
	assert(tile == Vector2i(128, 128), "Should convert back to (128, 128)")
	print("  ✓ PASS\n")
	
	# Test 3: MU to Godot Position
	print("Test 3: MU to Godot Position")
	var mu_pos = Vector3(12800.0, 12800.0, 150.0)  # MU units
	var godot_pos = MUCoordinates.mu_to_godot_position(mu_pos)
	print("  MU (12800, 12800, 150) -> Godot: ", godot_pos)
	print("  Expected: Vector3(128, 1.5, 128)")
	assert(abs(godot_pos.x - 128.0) < 0.001, "X should be 128")
	assert(abs(godot_pos.y - 1.5) < 0.001, "Y should be 1.5")
	assert(abs(godot_pos.z - 128.0) < 0.001, "Z should be 128")
	print("  ✓ PASS\n")
	
	# Test 4: Godot to MU Position
	print("Test 4: Godot to MU Position")
	var back_to_mu = MUCoordinates.godot_to_mu_position(godot_pos)
	print("  Godot (128, 1.5, 128) -> MU: ", back_to_mu)
	print("  Expected: Vector3(12800, 12800, 150)")
	assert(abs(back_to_mu.x - 12800.0) < 0.1, "X should be 12800")
	assert(abs(back_to_mu.y - 12800.0) < 0.1, "Y should be 12800")
	assert(abs(back_to_mu.z - 150.0) < 0.1, "Z should be 150")
	print("  ✓ PASS\n")
	
	# Test 5: Angle Conversion
	print("Test 5: MU Angle to Godot Rotation")
	var mu_angle = Vector3(0, 0, 45)  # 45° yaw around Z in MU
	var godot_rot = MUCoordinates.mu_angle_to_godot_rotation(mu_angle)
	print("  MU Angle (0, 0, 45°) -> Godot: ", godot_rot)
	print("  Expected: Y rotation ≈ 0.785 rad (45°)")
	assert(abs(godot_rot.y - deg_to_rad(45)) < 0.001, "Y rotation should be 45°")
	print("  ✓ PASS\n")
	
	# Test 6: Tile Index Conversion
	print("Test 6: Tile Index Conversion")
	var idx = MUCoordinates.tile_to_index(128, 128)
	print("  Tile (128, 128) -> Index: ", idx)
	print("  Expected: 32896 (128 * 256 + 128)")
	assert(idx == 32896, "Index should be 32896")
	var back_to_tile = MUCoordinates.index_to_tile(idx)
	print("  Index 32896 -> Tile: ", back_to_tile)
	assert(back_to_tile == Vector2i(128, 128), "Should convert back")
	print("  ✓ PASS\n")
	
	print("=== All Tests Passed! ===")
	quit()
