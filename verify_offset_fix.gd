@tool
extends SceneTree

const MUTerrain = preload("res://addons/mu_tools/mu_terrain.gd")
const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")

func _init():
	print("[Verify] Starting Offset Verification...")
	
	# Create terrain instance
	var terrain = MUTerrain.new()
	terrain.world_id = 1
	terrain.data_path = "res://reference/MuMain/src/bin/Data/World1"
	
	# Manually trigger parse to get raw data
	var parser = MUTerrainParser.new()
	var objects_file = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.obj"
	var raw_objects = parser.parse_objects_file(objects_file)
	
	print("[Verify] Parsed %d raw objects" % raw_objects.size())
	
	# Add terrain to scene creates children
	# We need to simulate _spawn_objects but extracting the comparison logic
	# Since _spawn_objects is called by load_world, let's just instantiate and check children
	
	# We can't easily run the full load_world in headless without a window sometimes, 
	# but let's try to just use the logic we modified.
	
	# Instead of full simulation, I will perform a targeted test of the logic 
	# by creating a dummy object and applying the transformation logic from the script
	# However, to be sure, I should let the real script run.
	
	var root = Node3D.new()
	root.add_child(terrain)
	
	# Override _spawn_objects to inspecting values? 
	# Easier: Just run load_world and inspect the children.
	
	# Problem: load_world needs resources. This might fail in minimal headless if paths are wrong.
	# Let's hope it works.
	
	print("[Verify] Running terrain.load_world()...")
	terrain.load_world()
	
	var spawned_count = 0
	var offset_correct_count = 0
	var error_count = 0
	
	print("[Verify] Checking spawned objects...")
	for child in terrain.get_children():
		# Objects are typically Node3D or MeshInstance3D or StaticBody3D children of terrain
		# Depending on structure. mu_terrain adds children directly.
		
		# We need to match spawned nodes to raw objects.
		# mu_terrain spawns them in order of object_data.
		# Let's assume order is preserved.
		
		if child is Node3D and not (child is MeshInstance3D and child.name == "TerrainMesh"):
			# Skip environment, cameras, etc.
			if child.name == "MUEnvironment" or child.name == "TerrainMesh" or child.name == "WaterMesh" or child.name == "GrassMesh":
				continue
				
			if spawned_count >= raw_objects.size():
				break
				
			var raw_obj = raw_objects[spawned_count]
			var spawned_pos = child.position
			var raw_pos = raw_obj.position
			# Correct Logic:
			# RawObj.position is MU Coordinates (X, Y, Z)
			# Godot Y = (MU Z * 0.01) + 5.0 (Our fix)
			
			var expected_y = (raw_obj.position.z * 0.01) + 5.0
			var diff = spawned_pos.y - expected_y
			
			if abs(diff) < 0.01:
				offset_correct_count += 1
			else:
				print("[Verify] ERROR: Object %d (%s) Height Mismatch! RawZ: %.2f, ExpectedY: %.2f, SpawnedY: %.2f, Diff: %.2f" % 
					[spawned_count, child.name, raw_obj.position.z, expected_y, spawned_pos.y, diff])
				error_count += 1
				
			spawned_count += 1
			
	print("[Verify] Checked %d objects." % spawned_count)
	print("[Verify] Correct offsets: %d" % offset_correct_count)
	print("[Verify] Errors: %d" % error_count)
	
	if error_count == 0 and offset_correct_count > 0:
		print("[Verify] SUCCESS: All checked objects have +5.0m offset.")
		quit(0)
	else:
		print("[Verify] FAILED: Offset mismatch detected.")
		quit(1)
