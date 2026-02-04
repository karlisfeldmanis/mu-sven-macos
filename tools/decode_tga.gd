extends SceneTree

## Standalone tool to decode MU TGA (.OZT) files
## Based on SVEN's GlobalBitmap::OpenTga implementation

func _init():
	print("=== MU TGA Decoder Tool ===")
	
	# Test with tree_08.OZT
	var test_file = "/Users/karlisfeldmanis/Desktop/mu_remaster/reference/MuMain/src/bin/Data/Object1/tree_08.OZT"
	
	if not FileAccess.file_exists(test_file):
		print("ERROR: Test file not found: ", test_file)
		quit(1)
		return
	
	print("\nTesting file: ", test_file.get_file())
	
	var result = decode_mu_tga(test_file)
	if result:
		print("\n✅ SUCCESS! TGA decoded successfully")
		print("  Size: ", result.width, "x", result.height)
		print("  Bit depth: ", result.bit_depth)
		print("  Components: ", result.components)
		print("  Pixel data size: ", result.pixel_data.size(), " bytes")
		
		# Try to create a Godot image
		var image = Image.create_from_data(
			result.width,
			result.height,
			false,
			Image.FORMAT_RGBA8,
			result.pixel_data
		)
		
		if image:
			print("\n✅ Godot Image created successfully!")
			print("  Saving to test_output.png...")
			var err = image.save_png("/Users/karlisfeldmanis/Desktop/mu_remaster/test_output.png")
			if err == OK:
				print("  ✅ Saved successfully!")
			else:
				print("  ❌ Save failed with error: ", err)
		else:
			print("\n❌ Failed to create Godot Image")
	else:
		print("\n❌ FAILED to decode TGA")
	
	quit(0)

func decode_mu_tga(path: String) -> Dictionary:
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		print("ERROR: Cannot open file")
		return {}
	
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	print("\nFile size: ", buffer.size(), " bytes")
	
	# Check minimum size
	if buffer.size() < 18:
		print("ERROR: File too small (< 18 bytes)")
		return {}
	
	# SVEN implementation:
	# int index = 12;
	# index += 4;  // Now at 16
	# Read width at 16, height at 18, bit at 20
	
	var index = 16
	
	# Read width (int16, little-endian)
	var width = buffer[index] | (buffer[index + 1] << 8)
	index += 2
	
	# Read height (int16, little-endian)
	var height = buffer[index] | (buffer[index + 1] << 8)
	index += 2
	
	# Read bit depth
	var bit_depth = buffer[index]
	index += 1
	
	# Skip 1 byte (SVEN does index += 1)
	index += 1
	
	print("\nHeader info:")
	print("  Width: ", width)
	print("  Height: ", height)
	print("  Bit depth: ", bit_depth)
	print("  Pixel data starts at offset: ", index)
	
	# Validate
	if bit_depth != 32:
		print("ERROR: Unsupported bit depth (expected 32, got ", bit_depth, ")")
		return {}
	
	if width <= 0 or height <= 0 or width > 4096 or height > 4096:
		print("ERROR: Invalid dimensions")
		return {}
	
	# Calculate expected pixel data size
	var expected_size = width * height * 4  # 4 bytes per pixel (BGRA)
	var available_size = buffer.size() - index
	
	print("  Expected pixel data: ", expected_size, " bytes")
	print("  Available data: ", available_size, " bytes")
	
	if available_size < expected_size:
		print("ERROR: Not enough pixel data")
		return {}
	
	# Read pixel data and convert BGRA to RGBA
	# SVEN reads bottom-to-top, left-to-right, BGRA format
	var pixel_data = PackedByteArray()
	pixel_data.resize(width * height * 4)
	
	for y in range(height):
		# SVEN: dst = &pNewBitmap->Buffer[(ny - 1 - y) * Width * Components];
		# This flips the image vertically
		var dst_offset = (height - 1 - y) * width * 4
		var src_offset = index + (y * width * 4)
		
		for x in range(width):
			var src_idx = src_offset + (x * 4)
			var dst_idx = dst_offset + (x * 4)
			
			# SVEN converts BGRA to RGBA:
			# dst[0] = src[2];  // R = B
			# dst[1] = src[1];  // G = G
			# dst[2] = src[0];  // B = R
			# dst[3] = src[3];  // A = A
			pixel_data[dst_idx + 0] = buffer[src_idx + 2]  # R
			pixel_data[dst_idx + 1] = buffer[src_idx + 1]  # G
			pixel_data[dst_idx + 2] = buffer[src_idx + 0]  # B
			pixel_data[dst_idx + 3] = buffer[src_idx + 3]  # A
	
	return {
		"width": width,
		"height": height,
		"bit_depth": bit_depth,
		"components": 4,
		"pixel_data": pixel_data
	}
