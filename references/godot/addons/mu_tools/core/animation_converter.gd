@tool
# class_name MUAnimationConverter

## Animation Converter (Phase 4)
## Translates MU BMD Actions to Godot AnimationLibrary.
##
## Note: BMD Action keys are Parent-Relative (local), matching Godot's expectation.

const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")

static func convert_actions(bmd_actions: Array, skeleton: Skeleton3D,
		_rotation_order: int = EULER_ORDER_ZYX) -> AnimationLibrary:
	var library = AnimationLibrary.new()
	
	# MuOnline standard is 24 FPS
	const MU_FPS = 24.0
	
	for i in range(bmd_actions.size()):
		var bmd_action = bmd_actions[i]
		var anim = Animation.new()
		
		# Ensure we use 24 FPS or the BMD specified FPS
		var fps = bmd_action.fps if bmd_action.fps > 0 else MU_FPS
		anim.length = bmd_action.frame_count / fps
		anim.loop_mode = Animation.LOOP_LINEAR if bmd_action.loop_flag else Animation.LOOP_NONE
		
		for bone_idx in range(bmd_action.bone_count):
			if bone_idx >= skeleton.get_bone_count(): break
			
			var bone_keys = bmd_action.keys[bone_idx]
			if bone_keys == null or bone_keys.is_empty(): continue # Skip dummy
			
			var bone_name = skeleton.get_bone_name(bone_idx)
			var track_path = NodePath("Skeleton3D:" + bone_name)
			
			var pos_track = anim.add_track(Animation.TYPE_POSITION_3D)
			anim.track_set_path(pos_track, track_path)
			
			var rot_track = anim.add_track(Animation.TYPE_ROTATION_3D)
			anim.track_set_path(rot_track, track_path)
			
			# Pass 2: Calculate local keys for this bone
			# MU BMD store relative positions per frame (parent-local).
			# Godot AnimationPlayer also expects local tracks.
			for f_idx in range(bone_keys.size()):
				var time = f_idx / fps
				var raw_pos = bone_keys[f_idx].position
				var raw_rot = bone_keys[f_idx].rotation
				
				# Direct conversion to Godot space
				var pos = MUTransformPipeline.local_mu_to_godot(raw_pos)
				var q = MUTransformPipeline.mu_rotation_to_quaternion(raw_rot)
				
				anim.position_track_insert_key(pos_track, time, pos)
				anim.rotation_track_insert_key(rot_track, time, q)
				
		library.add_animation("Action_" + str(i), anim)
		
	return library
