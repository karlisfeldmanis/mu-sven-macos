extends SceneTree

func _init():
	print("=== TGA Alpha Channel Diagnostic ===\n")
	
	var test_file = "/Users/karlisfeldmanis/Desktop/mu_remaster/reference/MuMain/src/bin/Data/Object1/tree_08.OZT"
	
	var file = FileAccess.open(test_file, FileAccess.READ)
	if not file:
		print("ERROR: Cannot open file")
		quit(1)
		return
	
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	print("File: tree_08.OZT")
	print("Size: ", buffer.size(), " bytes\n")
	
	# Read header
	var width = buffer[16] | (buffer[17] << 8)
	var height = buffer[18] | (buffer[19] << 8)
	var bit_depth = buffer[20]
	
	print("Dimensions: ", width, "x", height)
	print("Bit depth: ", bit_depth, "\n")
	
	# Sample first few pixels (BGRA format)
	print("First 10 pixels (BGRA format):")
	var index = 22  # Pixel data starts here
	for i in range(10):
		var offset = index + (i * 4)
		var b = buffer[offset + 0]
		var g = buffer[offset + 1]
		var r = buffer[offset + 2]
		var a = buffer[offset + 3]
		print("  Pixel ", i, ": B=", b, " G=", g, " R=", r, " A=", a)
	
	# Check alpha channel statistics
	print("\nAlpha channel analysis:")
	var alpha_zero = 0
	var alpha_nonzero = 0
	var alpha_255 = 0
	
	for y in range(height):
		for x in range(width):
			var pixel_offset = index + ((y * width + x) * 4)
			var a = buffer[pixel_offset + 3]
			if a == 0:
				alpha_zero += 1
			elif a == 255:
				alpha_255 += 1
			else:
				alpha_nonzero += 1
	
	var total = width * height
	print("  Total pixels: ", total)
	print("  Alpha = 0 (transparent): ", alpha_zero, " (", float(alpha_zero) / total * 100, "%)")
	print("  Alpha = 255 (opaque): ", alpha_255, " (", float(alpha_255) / total * 100, "%)")
	print("  Alpha = other: ", alpha_nonzero, " (", float(alpha_nonzero) / total * 100, "%)")
	
	quit(0)
