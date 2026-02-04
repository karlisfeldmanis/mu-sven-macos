extends SceneTree
# Automated test suite for SVEN-to-Godot coordinate transformation verification

const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")
const MUTerrainScript = preload("res://addons/mu_tools/mu_terrain.gd")

var parser: MUTerrainParser
var world_id = 1
var data_path = "res://reference/MuMain/src/bin/Data/World1"

func _init():
	print("======================================================================")
	print("SVEN TO GODOT COORDINATE MIGRATION TEST SUITE")
	print("======================================================================")
	print("")
	
	parser = MUTerrainParser.new()
	
	# Run all tests
	test_1_heightmap_parsing()
	test_2_object_height_analysis()
	test_3_coordinate_transformation()
	test_4_z_offset_hypothesis()
	
	print("")
	print("======================================================================")
	print("TEST SUITE COMPLETE")
	print("======================================================================")
	
	quit()

# =============================================================================
# TEST 1: Verify heightmap parsing matches SVEN format
# =============================================================================
func test_1_heightmap_parsing():
	print("\n[TEST 1] HEIGHTMAP PARSING VERIFICATION")
	print("----------------------------------------------------------------------")
	
	var height_file = data_path.path_join("TerrainHeight.OZB")
	var heightmap = parser.parse_height_file(height_file)
	
	if heightmap.is_empty():
		print("❌ FAILED: Could not load heightmap")
		return
	
	print("✓ Loaded heightmap: %d values" % heightmap.size())
	print("Expected: %d (256 x 256)" % (256 * 256))
	
	# Sample 10 positions and show their heights
	var samples = [
		Vector2(0, 0),
		Vector2(50, 50),
		Vector2(100, 100),
		Vector2(127, 127),
		Vector2(128, 128),
		Vector2(150, 150),
		Vector2(200, 200),
		Vector2(255, 0),
		Vector2(0, 255),
		Vector2(255, 255)
	]
	
	print("\nHeight samples (row-major indexing y*256+x):")
	print("%-12s %-10s %-10s" % ["Position", "Index", "Height (m)"])
	print("----------------------------------------")
	
	for pos in samples:
		var idx = int(pos.y) * 256 + int(pos.x)
		var height = heightmap[idx] if idx < heightmap.size() else -999.0
		print("(%3d, %3d)  %-10d %.3f" % [pos.x, pos.y, idx, height])
	
	# Statistical analysis
	var min_h = heightmap[0]
	var max_h = heightmap[0]
	var sum_h = 0.0
	
	for h in heightmap:
		min_h = min(min_h, h)
		max_h = max(max_h, h)
		sum_h += h
	
	var avg_h = sum_h / heightmap.size()
	
	print("\nHeightmap statistics:")
	print("  Min height: %.3f m" % min_h)
	print("  Max height: %.3f m" % max_h)
	print("  Avg height: %.3f m" % avg_h)
	print("  Range:      %.3f m" % (max_h - min_h))

# =============================================================================
# TEST 2: Analyze raw object heights from .obj file
# =============================================================================
func test_2_object_height_analysis():
	print("\n[TEST 2] OBJECT FILE HEIGHT ANALYSIS")
	print("----------------------------------------------------------------------")
	
	# Load heightmap for comparison
	var height_file = data_path.path_join("TerrainHeight.OZB")
	var heightmap = parser.parse_height_file(height_file)
	
	# Load objects
	var objects_file = data_path.path_join("EncTerrain1.obj")
	var objects = parser.parse_objects_file(objects_file)
	
	if objects.is_empty():
		print("❌ FAILED: Could not load objects file")
		return
	
	print("✓ Loaded %d objects from .obj file" % objects.size())
	
	# Analyze first 20 objects
	var sample_size = min(20, objects.size())
	print("\nAnalyzing first %d objects:" % sample_size)
	print("")
	print("%-6s %-15s %-12s %-12s %-12s" % [
		"Type", "MU Position", "Raw Z", "Terrain H", "Z-Terrain"
	])
	print("----------------------------------------------------------------------")
	
	var offsets = []
	
	for i in range(sample_size):
		var obj = objects[i]
		
		# Raw SVEN coordinates (from .obj file)
		var raw_x = obj.mu_pos_raw.x  # SVEN X
		var raw_y = obj.mu_pos_raw.y  # SVEN Y  
		var raw_z = obj.mu_pos_raw.z  # SVEN Z (height)
		
		# Convert to meters
		var mu_x_m = raw_x / 100.0
		var mu_y_m = raw_y / 100.0
		var mu_z_m = raw_z / 100.0
		
		# Get terrain height at this position
		# SVEN indexing: y*256+x where x,y are tile coordinates
		var tile_x = int(mu_x_m)
		var tile_y = int(mu_y_m)
		var idx = tile_y * 256 + tile_x
		
		var terrain_h = heightmap[idx] if idx < heightmap.size() else 0.0
		var offset = mu_z_m - terrain_h
		
		offsets.append(offset)
		
		print("%-6d (%.1f,%.1f,%.1f)  %-12.2f %-12.2f %+.2f" % [
			obj.type,
			mu_x_m, mu_y_m, mu_z_m,
			mu_z_m,
			terrain_h,
			offset
		])
	
	# Statistical analysis of offsets
	if not offsets.is_empty():
		var sum_offset = 0.0
		var min_offset = offsets[0]
		var max_offset = offsets[0]
		
		for offset in offsets:
			sum_offset += offset
			min_offset = min(min_offset, offset)
			max_offset = max(max_offset, offset)
		
		var avg_offset = sum_offset / offsets.size()
		
		print("")
		print("OFFSET STATISTICS (Z - TerrainHeight):")
		print("  Average: %+.3f m" % avg_offset)
		print("  Min:     %+.3f m" % min_offset)
		print("  Max:     %+.3f m" % max_offset)
		print("  Range:   %.3f m" % (max_offset - min_offset))
		print("")
		
		# Hypothesis testing
		if abs(avg_offset - (-5.0)) < 0.5:
			print("✓ HYPOTHESIS SUPPORTED: Average offset ≈ -5.0m")
			print("  → Objects are stored relative to SVEN terrain baseline (-5.0m)")
			print("  → +5.0m offset in Godot is CORRECT")
		elif abs(avg_offset) < 0.5:
			print("⚠ HYPOTHESIS REJECTED: Average offset ≈ 0.0m")
			print("  → Objects are stored at absolute terrain height")
			print("  → +5.0m offset in Godot is INCORRECT (remove it)")
		else:
			print("⚠ UNEXPECTED RESULT: Average offset = %.2fm" % avg_offset)
			print("  → Need further investigation")
			print("  → Correct offset should be: %+.2fm" % (-avg_offset))

# =============================================================================
# TEST 3: Verify coordinate transformation logic
# =============================================================================
func test_3_coordinate_transformation():
	print("\n[TEST 3] COORDINATE TRANSFORMATION VERIFICATION")
	print("----------------------------------------------------------------------")
	
	# Test known transformations
	var test_cases = [
		{
			"name": "Origin",
			"sven": Vector3(0, 0, 0),
			"expected_godot": Vector3(0, 0, -255)
		},
		{
			"name": "Center",
			"sven": Vector3(12800, 12800, 500),
			"expected_godot": Vector3(128, 5, -127)
		},
		{
			"name": "Max corner",
			"sven": Vector3(25500, 25500, 1000),
			"expected_godot": Vector3(255, 10, 0)
		}
	]
	
	print("\nTesting SVEN → Godot transformation:")
	print("Formula: godot(x,y,z) = (sven_y/100, sven_z/100, sven_x/100 - 255)")
	print("")
	print("%-15s %-20s %-20s" % ["Test Case", "SVEN (x,y,z)", "Godot (x,y,z)"])
	print("----------------------------------------------------------------------")
	
	for test in test_cases:
		var sven_pos = test.sven
		var expected = test.expected_godot
		
		# Apply transformation (without +5.0 offset for now)
		var godot_pos = Vector3(
			sven_pos.y / 100.0,
			sven_pos.z / 100.0,
			sven_pos.x / 100.0 - 255.0
		)
		
		var matches = godot_pos.is_equal_approx(expected)
		var status = "✓" if matches else "❌"
		
		print("%s %-12s (%.0f,%.0f,%.0f)    → (%.1f,%.1f,%.1f)" % [
			status,
			test.name,
			sven_pos.x, sven_pos.y, sven_pos.z,
			godot_pos.x, godot_pos.y, godot_pos.z
		])

# =============================================================================
# TEST 4: Test the +5.0m offset hypothesis with real objects
# =============================================================================
func test_4_z_offset_hypothesis():
	print("\n[TEST 4] +5.0M OFFSET HYPOTHESIS VERIFICATION")
	print("----------------------------------------------------------------------")
	
	# Load data
	var height_file = data_path.path_join("TerrainHeight.OZB")
	var heightmap = parser.parse_height_file(height_file)
	var objects_file = data_path.path_join("EncTerrain1.obj")
	var objects = parser.parse_objects_file(objects_file)
	
	if objects.is_empty() or heightmap.is_empty():
		print("❌ FAILED: Could not load required files")
		return
	
	print("\nComparing object placement WITH and WITHOUT +5.0m offset:")
	print("")
	print("%-6s %-15s %-12s %-15s %-15s" % [
		"Type", "Godot Pos", "Terrain H", "No Offset Δ", "With +5.0 Δ"
	])
	print("----------------------------------------------------------------------")
	
	var errors_no_offset = []
	var errors_with_offset = []
	
	for i in range(min(15, objects.size())):
		var obj = objects[i]
		
		# Transform to Godot coordinates
		var godot_pos = obj.position  # Already transformed by parser
		
		# Get terrain height at this Godot position
		# Godot X = MU Y, Godot Z = MU X - 255
		# Reverse: MU Y = Godot X, MU X = Godot Z + 255
		var mu_y = int(godot_pos.x)
		var mu_x = int(godot_pos.z + 255)
		
		if mu_x < 0 or mu_x >= 256 or mu_y < 0 or mu_y >= 256:
			continue
		
		var idx = mu_y * 256 + mu_x
		var terrain_h = heightmap[idx]
		
		# Calculate errors
		var error_no_offset = godot_pos.y - terrain_h
		var error_with_offset = (godot_pos.y + 5.0) - terrain_h
		
		errors_no_offset.append(abs(error_no_offset))
		errors_with_offset.append(abs(error_with_offset))
		
		print("%-6d (%.1f,%.1f,%.1f) %-12.2f %+7.2f m      %+7.2f m" % [
			obj.type,
			godot_pos.x, godot_pos.y, godot_pos.z,
			terrain_h,
			error_no_offset,
			error_with_offset
		])
	
	# Compare average errors
	if not errors_no_offset.is_empty():
		var avg_err_no = errors_no_offset.reduce(func(a,b): return a+b, 0.0) / errors_no_offset.size()
		var avg_err_with = errors_with_offset.reduce(func(a,b): return a+b, 0.0) / errors_with_offset.size()
		
		print("")
		print("AVERAGE ABSOLUTE ERROR:")
		print("  Without offset: %.3f m" % avg_err_no)
		print("  With +5.0m:     %.3f m" % avg_err_with)
		print("")
		
		if avg_err_with < avg_err_no:
			print("✓ CONCLUSION: +5.0m offset IMPROVES accuracy")
			print("  → Keep the +5.0m offset in mu_terrain.gd")
		else:
			print("❌ CONCLUSION: +5.0m offset WORSENS accuracy")
			print("  → Remove the +5.0m offset from mu_terrain.gd")
