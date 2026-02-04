extends SceneTree

func _init():
	print("=== Comparing Pixel Data ===\n")
	
	var test_file = "/Users/karlisfeldmanis/Desktop/mu_remaster/reference/MuMain/src/bin/Data/Object1/tree_08.OZT"
	
	var file = FileAccess.open(test_file, FileAccess.READ)
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	var width = buffer[16] | (buffer[17] << 8)
	var height = buffer[18] | (buffer[19] << 8)
	
	print("Dimensions: ", width, "x", height)
	print("\nFirst row of pixels (showing both BGRA source and RGBA output):\n")
	
	var index = 22  # Pixel data starts here
	
	for x in range(min(10, width)):
		var src_offset = index + (x * 4)
		
		# Source BGRA
		var b_src = buffer[src_offset + 0]
		var g_src = buffer[src_offset + 1]
		var r_src = buffer[src_offset + 2]
		var a_src = buffer[src_offset + 3]
		
		# What we convert to (RGBA with vertical flip)
		# For first row (y=0), after flip it goes to last row (y=31)
		var dst_y = height - 1 - 0  # = 31
		var dst_offset = (dst_y * width + x) * 4
		
		print("Pixel [", x, ",0]:")
		print("  Source BGRA: B=", b_src, " G=", g_src, " R=", r_src, " A=", a_src)
		print("  Output RGBA: R=", r_src, " G=", g_src, " B=", b_src, " A=", a_src)
		
		if a_src > 0:
			print("  -> VISIBLE (alpha > 0)")
		else:
			print("  -> TRANSPARENT (alpha = 0)")
	
	# Check if there are ANY opaque pixels with bright colors
	print("\nSearching for bright opaque pixels...")
	var found_bright = 0
	for y in range(height):
		for x in range(width):
			var offset = index + ((y * width + x) * 4)
			var r = buffer[offset + 2]
			var g = buffer[offset + 1]
			var b = buffer[offset + 0]
			var a = buffer[offset + 3]
			
			if a > 128 and (r > 100 or g > 100 or b > 100):
				if found_bright < 5:
					print("  Found at [", x, ",", y, "]: R=", r, " G=", g, " B=", b, " A=", a)
				found_bright += 1
	
	print("\nTotal bright opaque pixels found: ", found_bright)
	
	quit(0)
