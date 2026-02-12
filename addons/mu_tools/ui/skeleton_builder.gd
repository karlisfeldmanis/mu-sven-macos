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
	
	print("[Skeleton Builder] Skeleton bone count: ", skeleton.get_bone_count())
	
	# Pass 2: Set parent-child relationships and rest transforms
	for i in range(bmd_bones.size()):
		var bmd_bone = bmd_bones[i]
		
		if bmd_bone.parent_index >= 0 and bmd_bone.parent_index < bmd_bones.size():
			skeleton.set_bone_parent(i, bmd_bone.parent_index)
		
		# Rest pose from action 0, frame 0 (MU native coordinates)
		var rest_pos = Vector3.ZERO
		var rest_basis = Basis.IDENTITY
		
		if not bmd_actions.is_empty():
			var action = bmd_actions[0]
			if action.keys.size() > i and action.keys[i] != null and not action.keys[i].is_empty():
				var key = action.keys[i][0]
				# 🔴 FIX: Use LOCAL coordinate mapping for bones, NOT world mirroring
				rest_pos = MUTransformPipeline.local_mu_to_godot(key.position)
				# 🔴 FIX: Use authoritative rotation conversion
				var q = MUTransformPipeline.mu_rotation_to_quaternion(key.rotation)
				rest_basis = Basis(q)
		
		var rest_transform = Transform3D()
		rest_transform.origin = rest_pos
		rest_transform.basis = rest_basis
		
		skeleton.set_bone_rest(i, rest_transform)
		
	return skeleton
