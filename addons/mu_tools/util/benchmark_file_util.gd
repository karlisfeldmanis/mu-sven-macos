extends SceneTree

func _init():
	print("\n=== MUFileUtil Caching Benchmark ===\n")
	
	var MUFileUtil = load("res://addons/mu_tools/core/file_util.gd")
	var test_path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.obj"
	
	# Warm up (resolve once to fill cache if we want to test pure cache speed)
	# But let's test Cold vs Warm.
	
	print("Test Path: ", test_path)
	
	# 1. Cold Start (first time)
	var start_time = Time.get_ticks_usec()
	var resolved = MUFileUtil.resolve_case(test_path)
	var end_time = Time.get_ticks_usec()
	print("Cold Resolve: %d us" % (end_time - start_time))
	print("Resolved to: ", resolved)
	
	# 2. Warm Start (Path Cache - Level 1)
	start_time = Time.get_ticks_usec()
	resolved = MUFileUtil.resolve_case(test_path)
	end_time = Time.get_ticks_usec()
	print("Warm Resolve (L1 Cache): %d us" % (end_time - start_time))
	
	# 3. Directory Cache Test (Level 2)
	# Test with a different file in the same directory
	var test_path_2 = "res://reference/MuMain/src/bin/Data/World1/Terrain1.att"
	start_time = Time.get_ticks_usec()
	resolved = MUFileUtil.resolve_case(test_path_2)
	end_time = Time.get_ticks_usec()
	print("Warm Resolve (L2 Cache - same dir): %d us" % (end_time - start_time))
	
	# 4. Stress Test (1000 iterations)
	print("\nRunning 1000 iterations...")
	start_time = Time.get_ticks_usec()
	for i in range(1000):
		MUFileUtil.resolve_case(test_path)
	end_time = Time.get_ticks_usec()
	print("1000 iterations: %d us (Avg: %.2f us/op)" % [(end_time - start_time), (end_time - start_time) / 1000.0])

	quit()
