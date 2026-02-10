extends SceneTree

func _init():
	var BMD_PATH = "reference/MuMain/src/bin/Data/Object1/PoseBox01.bmd"
	var TEX_NAME = "ston03.jpg"
	
	var resolver = load("res://addons/mu_tools/util/mu_texture_resolver.gd")
	var path = resolver.resolve_texture_path(BMD_PATH, TEX_NAME)
	
	print("BMD Path: ", BMD_PATH)
	print("Texture Name: ", TEX_NAME)
	print("Resolved Path: '", path, "'")
	
	if not path.is_empty():
		var f = FileAccess.open(path, FileAccess.READ)
		var buf = f.get_buffer(f.get_length())
		f.close()
		
		if buf.size() > 454:
			var img454 = Image.new()
			var err454 = img454.load_jpg_from_buffer(buf.slice(454))
			print("Offset 454: ", err454, " Size: ", img454.get_width(), "x", img454.get_height() if err454 == OK else "N/A")
			if err454 == OK:
				print("Color at 454 (0,0): ", img454.get_pixel(0,0))
	else:
		print("Resolution FAILED!")
		
	quit()
