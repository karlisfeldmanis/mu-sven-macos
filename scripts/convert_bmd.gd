@tool
extends SceneTree

# Headless BMD Converter CLI

func _init():
	var args = OS.get_cmdline_user_args()
	var input_path = ""
	var output_dir = ""
	var debug = false
	
	for i in range(args.size()):
		if args[i] == "--input" and i + 1 < args.size():
			input_path = args[i + 1]
		elif args[i] == "--output" and i + 1 < args.size():
			output_dir = args[i + 1]
		elif args[i] == "--debug":
			debug = true

	if input_path == "" or output_dir == "":
		print("Usage: godot --headless -s convert_bmd.gd -- --input <file.bmd> --output <res://path/> [--debug]")
		quit()
		return

	_convert(input_path, output_dir, debug)
	quit()

func _convert(input_path: String, output_dir: String, debug: bool):
	print("[CLI] Converting: ", input_path)
	
	var parser = BMDParser.new()
	if not parser.parse_file(input_path, debug):
		push_error("[CLI] Failed to parse BMD: " + input_path)
		return

	# 1. Build Skeleton
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	
	# 2. Create root with Z-up to Y-up rotation
	var root = Node3D.new()
	root.name = input_path.get_file().get_basename()
	# Apply -90° X rotation to convert from Z-up to Y-up
	root.rotation = MUCoordinateUtils.get_root_rotation()
	
	root.add_child(skeleton)
	skeleton.owner = root
	
	# Create Skin using Inverse Global Rest
	var skin = Skin.new()
	for j in range(skeleton.get_bone_count()):
		skin.add_bind(j, skeleton.get_bone_global_rest(j).inverse())
	
	for i in range(parser.meshes.size()):
		var bmd_mesh = parser.meshes[i]
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton)
		if mesh_instance:
			skeleton.add_child(mesh_instance)
			mesh_instance.owner = root
			mesh_instance.skin = skin
			mesh_instance.skeleton = mesh_instance.get_path_to(skeleton)
			mesh_instance.name = "Mesh_%d" % i

	# 3. Convert Animations
	var anim_library = MUAnimationConverter.convert_actions(parser.actions, skeleton)
	if anim_library:
		var anim_player = AnimationPlayer.new()
		anim_player.name = "AnimationPlayer"
		root.add_child(anim_player)
		anim_player.owner = root
		anim_player.add_animation_library("", anim_library)

	# 4. Save Scene
	if not DirAccess.dir_exists_absolute(output_dir):
		DirAccess.make_dir_recursive_absolute(output_dir)
		
	var scene_path = output_dir.path_join(root.name + ".tscn")
	var packed_scene = PackedScene.new()
	packed_scene.pack(root)
	
	var err = ResourceSaver.save(packed_scene, scene_path)
	if err == OK:
		print("[CLI] Successfully saved scene to: ", scene_path)
	else:
		push_error("[CLI] Failed to save scene: ", err)
