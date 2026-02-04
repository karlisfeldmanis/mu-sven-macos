extends SceneTree

func _init():
	var args = OS.get_cmdline_user_args()
	var folder = ""
	if args.size() > 0:
		folder = args[0]
	
	if folder == "" or not DirAccess.dir_exists_absolute(folder):
		print("Usage: godot --headless -s inspect_bmd_textures.gd -- <folder_path>")
		print("Provided folder: '", folder, "'")
		quit()
	var dir = DirAccess.open(folder)
	if not dir:
		print("Cannot open folder: ", folder)
		quit()
		
	dir.list_dir_begin()
	var filename = dir.get_next()
	
	var BMDParser = load("res://addons/mu_tools/bmd_parser.gd")
	
	while filename != "":
		if filename.to_lower().ends_with(".bmd"):
			var path = folder.path_join(filename)
			var parser = BMDParser.new()
			if parser.parse_file(path, false):
				print("BMD: ", filename)
				for i in range(parser.meshes.size()):
					var m = parser.meshes[i]
					print("  Mesh %d: Texture='%s' Flags=%d" % [i, m.texture_filename, m.flags])
			else:
				print("FAILED to parse: ", filename)
		filename = dir.get_next()
	
	quit()
