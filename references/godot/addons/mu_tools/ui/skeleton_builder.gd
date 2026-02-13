@tool
# class_name MUSkeletonBuilder

## Skeleton Builder (Phase 3)
## Translates MU bone hierarchy to Godot Skeleton3D.

const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")

static func build_skeleton(bmd_bones: Array, bmd_actions: Array = []) -> Skeleton3D:
	var skeleton = Skeleton3D.new()
	
	# Pass 1: Add all bones to the skeleton
	for i in range(bmd_bones.size()):
		var bone = bmd_bones[i]
		var bname = bone.name
		if bname.is_empty():
			bname = "Bone_%d" % i
		
		var idx = skeleton.add_bone(bname)
		if idx == -1:
			push_error("[Skeleton Builder] Failed to add bone: " + bname)
	
	# Pass 2: Set parent-child relationships
	for i in range(bmd_bones.size()):
		var bmd_bone = bmd_bones[i]
		if bmd_bone.parent_index >= 0 and bmd_bone.parent_index < bmd_bones.size():
			skeleton.set_bone_parent(i, bmd_bone.parent_index)
	
	# Pass 3: Resolve Rest Poses (Hierarchical)
	# MU BMD data stores LOCAL (parent-relative) transforms for each bone key.
	# We use Action 0 or base bone data as the rest pose.
	for i in range(bmd_bones.size()):
		var bmd_bone = bmd_bones[i]
		
		var raw_pos = bmd_bone.position
		var raw_rot = bmd_bone.rotation
		
		# Prefer Action 0 Frame 0 for accurate bind pose alignment
		if not bmd_actions.is_empty():
			var action = bmd_actions[0]
			if action.keys.size() > i and action.keys[i] != null and not action.keys[i].is_empty():
				var key = action.keys[i][0]
				raw_pos = key.position
				raw_rot = key.rotation
				
		var pos = MUTransformPipeline.local_mu_to_godot(raw_pos)
		var basis = Basis(MUTransformPipeline.mu_rotation_to_quaternion(raw_rot))
		var local_rest = Transform3D(basis, pos)
			
		skeleton.set_bone_rest(i, local_rest)
		
	# Pass 4: Pre-calculate bone_global_pose for reference
	# skeleton.reset_bone_poses() 
	
	print("[Skeleton Builder] Skeleton bone count: ", skeleton.get_bone_count())
	return skeleton
