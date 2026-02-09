@tool
class_name MUAnimationBuilder

## Helper to build Godot Animation resources from BMD actions
##
## Converts MU's bone-local keyframes into Godot skeletal animation tracks.

static func build_animation_library(path: String, actions: Array[BMDParser.BMDAction], 
		skeleton: Skeleton3D) -> AnimationLibrary:
	var library = AnimationLibrary.new()
	
	const MUAnimationRegistry = preload("res://addons/mu_tools/util/mu_animation_registry.gd")
	for i in range(actions.size()):
		var action_data = actions[i]
		var action_name = MUAnimationRegistry.get_action_name(path, i)
		var is_loop = MUAnimationRegistry.is_looping(path, i)
		
		# Get PlaySpeed from registry for baking
		var play_speed = MUAnimationRegistry.get_play_speed(path, i)
		var anim = build_animation(action_data, skeleton, is_loop, play_speed)
		
		library.add_animation(action_name, anim)
			
	return library

static func build_animation(action: BMDParser.BMDAction, 
		skeleton: Skeleton3D, 
		looping: bool = true,
		play_speed: float = 1.0) -> Animation:
	if action.frame_count <= 0 or action.keys.is_empty():
		return null
		
	var anim = Animation.new()
	
	# Effective FPS (Base 25.0 * MU PlaySpeed multiplier)
	var effective_fps = action.fps * play_speed
	
	anim.length = action.frame_count / effective_fps
	anim.step = 1.0 / effective_fps
	
	if looping:
		anim.loop_mode = Animation.LOOP_LINEAR
	
	# Skeleton path (relative to AnimationPlayer, which we assume is sibling to Skeleton)
	var skeleton_path = "Skeleton" 
	
	for bone_idx in range(action.bone_count):
		if bone_idx >= skeleton.get_bone_count():
			continue
			
		var bone_name = skeleton.get_bone_name(bone_idx)
		var track_path = skeleton_path + ":" + bone_name
		
		# Create tracks
		var pos_track = anim.add_track(Animation.TYPE_POSITION_3D)
		anim.track_set_path(pos_track, track_path)
		
		var rot_track = anim.add_track(Animation.TYPE_ROTATION_3D)
		anim.track_set_path(rot_track, track_path)
		
		var bone_keys = action.keys[bone_idx]
		if bone_keys == null or bone_keys.is_empty():
			continue
			
		for f in range(action.frame_count):
			var time = f / effective_fps
			var key = bone_keys[f]
			
			# Position conversion (MU Native -> MU Ref Scale)
			var pos = MUCoordinateUtils.convert_position(key.position)
			anim.position_track_insert_key(pos_track, time, pos)
			
			# Rotation conversion (MU Quat)
			var rot = key.converted_quat
			anim.rotation_track_insert_key(rot_track, time, rot)
			
	return anim
