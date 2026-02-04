extends SceneTree

func _init():
	print("=== Verifying TGA Fix ===\n")
	
	var loader = preload("res://addons/mu_tools/mu_texture_loader.gd")
	var test_file = "/Users/karlisfeldmanis/Desktop/mu_remaster/reference/MuMain/src/bin/Data/Object1/tree_08.OZT"
	
	if not FileAccess.file_exists(test_file):
		print("❌ Test file not found: ", test_file)
		quit(1)
		return

	var texture = loader.load_mu_texture(test_file)
	if texture:
		var image = texture.get_image()
		print("✅ Successfully loaded OZT: ", test_file)
		print("   Size: ", image.get_width(), "x", image.get_height())
		print("   Format: ", image.get_format())
		
		# Sample a few pixels to check alpha
		var has_transparency = false
		for y in range(0, image.get_height(), 16):
			for x in range(0, image.get_width(), 16):
				var pixel = image.get_pixel(x, y)
				if pixel.a < 0.9:
					has_transparency = true
					break
			if has_transparency: break
			
		if has_transparency:
			print("✅ Preserved transparency detected in OZT!")
		else:
			print("⚠️ No transparency detected in sample (could be a solid texture)")
			
		var save_err = image.save_png("/Users/karlisfeldmanis/Desktop/mu_remaster/test_ozt_fixed.png")
		if save_err == OK:
			print("✅ Saved verification image to test_ozt_fixed.png")
		else:
			print("❌ Failed to save verification image")
	else:
		print("❌ FAILED to load OZT: ", test_file)
	
	quit(0)
