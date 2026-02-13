extends SceneTree

func _init():
	var args = OS.get_cmdline_user_args()
	print("User Args: ", args)
	var bmd_path = ""
	for i in range(args.size()):
		if args[i] == "--input" and i + 1 < args.size():
			bmd_path = args[i+1]
			break
	
	if bmd_path == "":
		print("Usage: godot -s inspect_bmd.gd -- --input path/to.bmd")
		quit()
		return

	var parser = load("res://addons/mu_tools/parsers/bmd_parser.gd").new()
	if parser.parse_file(bmd_path):
		print("BMD: ", bmd_path.get_file())
		print("  Bones: ", parser.bones.size())
		print("  Actions: ", parser.actions.size())
		print("  Meshes: ", parser.meshes.size())
		for i in range(parser.meshes.size()):
			var mesh = parser.meshes[i]
			var nodes = []
			for n in mesh.vertex_nodes:
				if not n in nodes: nodes.append(n)
			print("    Mesh %d: %s (Bones: %s)" % [i, mesh.texture_filename, nodes])
		if parser.actions.size() > 0:
			for i in range(parser.actions.size()):
				print("    Action %d: %d frames" % [i, parser.actions[i].frame_count])
	else:
		print("Failed to parse BMD")
	
	quit()
