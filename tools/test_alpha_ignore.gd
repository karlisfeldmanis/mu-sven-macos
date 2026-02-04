extends SceneTree

## Test: What if we IGNORE alpha and set it to 255?

func _init():
	print("=== Testing Alpha Ignore ===\n")
	
	var test_file = "/Users/karlisfeldmanis/Desktop/mu_remaster/reference/MuMain/src/bin/Data/Object1/tree_08.OZT"
	
	var file = FileAccess.open(test_file, FileAccess.READ)
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	var width = buffer[16] | (buffer[17] << 8)
	var height = buffer[18] | (buffer[19] << 8)
	var index = 22
	
	# Convert with ALPHA FORCED TO 255
	var pixel_data = PackedByteArray()
	pixel_data.resize(width * height * 4)
	
	for y in range(height):
		var dst_offset = (height - 1 - y) * width * 4
		var src_offset = index + (y * width * 4)
		
		for x in range(width):
			var src_idx = src_offset + (x * 4)
			var dst_idx = dst_offset + (x * 4)
			
			pixel_data[dst_idx + 0] = buffer[src_idx + 2]  # R
			pixel_data[dst_idx + 1] = buffer[src_idx + 1]  # G
			pixel_data[dst_idx + 2] = buffer[src_idx + 0]  # B
			pixel_data[dst_idx + 3] = 255  # FORCE ALPHA TO 255 (fully opaque)
	
	var image = Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, pixel_data)
	
	if image:
		print("✅ Image created with forced alpha=255")
		var err = image.save_png("/Users/karlisfeldmanis/Desktop/mu_remaster/test_alpha_255.png")
		if err == OK:
			print("✅ Saved to test_alpha_255.png")
		else:
			print("❌ Save failed")
	
	quit(0)
