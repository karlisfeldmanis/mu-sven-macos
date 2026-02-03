extends SceneTree

## Headless Skeleton Test
## Prints skeleton structure to console for verification

func _init():
	var path = "raw_data/Player/Player.bmd"
	
	if not FileAccess.file_exists(path):
		print("✗ File not found: ", path)
		quit()
		return
	
	print("=".repeat(60))
	print("SKELETON STRUCTURE TEST")
	print("=".repeat(60))
	print("File: ", path)
	
	# Parse BMD
	var parser = BMDParser.new()
	if not parser.parse_file(path, false):
		print("✗ Failed to parse BMD")
		quit()
		return
	
	print("\n✓ BMD parsed successfully")
	print("  Meshes: ", parser.meshes.size())
	print("  Bones: ", parser.bones.size())
	print("  Actions: ", parser.actions.size())
	
	# Build skeleton
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	if not skeleton:
		print("✗ Failed to build skeleton")
		quit()
		return
	
	print("\n✓ Skeleton built successfully")
	print("  Bone count: ", skeleton.get_bone_count())
	
	# Analyze skeleton structure
	print("\n" + "=".repeat(60))
	print("BONE HIERARCHY")
	print("=".repeat(60))
	
	var root_bones = []
	for i in range(skeleton.get_bone_count()):
		var bone_name = skeleton.get_bone_name(i)
		var parent_idx = skeleton.get_bone_parent(i)
		var rest = skeleton.get_bone_rest(i)
		var global_rest = skeleton.get_bone_global_rest(i)
		
		if parent_idx == -1:
			root_bones.append(bone_name)
		
		# Calculate depth for indentation
		var depth = 0
		var check_idx = parent_idx
		while check_idx >= 0:
			depth += 1
			check_idx = skeleton.get_bone_parent(check_idx)
		
		var indent = "  ".repeat(depth)
		var parent_name = skeleton.get_bone_name(parent_idx) if parent_idx >= 0 else "ROOT"
		
		print("\n%s[%d] %s" % [indent, i, bone_name])
		print("%s    Parent: %s (%d)" % [indent, parent_name, parent_idx])
		print("%s    Local:  %s" % [indent, rest.origin])
		print("%s    Global: %s" % [indent, global_rest.origin])
	
	print("\n" + "=".repeat(60))
	print("SUMMARY")
	print("=".repeat(60))
	print("Root bones: ", root_bones)
	print("Total bones: ", skeleton.get_bone_count())
	
	# Check for humanoid characteristics
	var bone_names_lower = []
	for i in range(skeleton.get_bone_count()):
		bone_names_lower.append(skeleton.get_bone_name(i).to_lower())
	
	var has_spine = bone_names_lower.any(func(n): return "spine" in n or "pelvis" in n)
	var has_head = bone_names_lower.any(func(n): return "head" in n)
	var has_arms = bone_names_lower.any(func(n): return "arm" in n or "hand" in n)
	var has_legs = bone_names_lower.any(func(n): return "leg" in n or "thigh" in n or "foot" in n)
	
	print("\nHumanoid Characteristics Check:")
	print("  Spine/Pelvis: ", "✓" if has_spine else "✗")
	print("  Head: ", "✓" if has_head else "✗")
	print("  Arms: ", "✓" if has_arms else "✗")
	print("  Legs: ", "✓" if has_legs else "✗")
	
	if has_spine and has_head and has_arms and has_legs:
		print("\n✓✓✓ LIKELY HUMANOID SKELETON ✓✓✓")
	else:
		print("\n⚠ May not be complete humanoid structure")
	
	print("=".repeat(60))
	quit()
