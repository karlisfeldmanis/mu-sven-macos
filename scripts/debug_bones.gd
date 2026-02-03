extends SceneTree

## Debug bone transforms to identify the issue

func _init():
	var path = "raw_data/Player/Player.bmd"
	
	if not FileAccess.file_exists(path):
		print("✗ File not found")
		quit()
		return
	
	print("=".repeat(60))
	print("BONE TRANSFORM DEBUG")
	print("=".repeat(60))
	
	# Parse BMD
	var parser = BMDParser.new()
	if not parser.parse_file(path, false):
		print("✗ Failed to parse")
		quit()
		return
	
	# Build skeleton
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	if not skeleton:
		print("✗ Failed to build skeleton")
		quit()
		return
	
	print("\n✓ Skeleton built: %d bones\n" % skeleton.get_bone_count())
	
	# Check first 10 bones in detail
	for i in range(min(10, skeleton.get_bone_count())):
		var bone_name = skeleton.get_bone_name(i)
		var parent_idx = skeleton.get_bone_parent(i)
		var local_rest = skeleton.get_bone_rest(i)
		var global_rest = skeleton.get_bone_global_rest(i)
		
		print("[%d] %s (parent: %d)" % [i, bone_name, parent_idx])
		print("  Local REST:")
		print("    Origin: %s" % local_rest.origin)
		print("    Basis.x: %s" % local_rest.basis.x)
		print("    Basis.y: %s" % local_rest.basis.y)
		print("    Basis.z: %s" % local_rest.basis.z)
		print("  Global REST:")
		print("    Origin: %s" % global_rest.origin)
		print("")
	
	# Check the raw BMD data
	print("\n" + "=".repeat(60))
	print("RAW BMD DATA (Action 0, Frame 0)")
	print("=".repeat(60))
	
	if not parser.actions.is_empty():
		var action = parser.actions[0]
		for i in range(min(10, parser.bones.size())):
			var bone = parser.bones[i]
			print("\n[%d] %s" % [i, bone.name])
			
			if action.keys[i] != null and not action.keys[i].is_empty():
				var key = action.keys[i][0]
				print("  RAW Position: %s" % key.position)
				print("  RAW Rotation: %s (radians)" % key.rotation)
				print("  Converted Pos: %s" % MUCoordinateUtils.convert_position(key.position))
				var quat = MUCoordinateUtils.bmd_angle_to_quaternion(key.rotation)
				print("  Converted Quat: %s" % quat)
	
	print("\n" + "=".repeat(60))
	quit()
