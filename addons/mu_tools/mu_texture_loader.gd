@tool
class_name MUTextureLoader

const MUDecryptor = preload("res://addons/mu_tools/mu_decryptor.gd")
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")

## Directly load and decrypt an OZJ/OZT file as an ImageTexture
static func load_mu_texture(path: String) -> ImageTexture:
	var file = FileAccess.open(path, FileAccess.READ)
	if not file: 
		# Attempt case-insensitive recovery
		var dir_path = path.get_base_dir()
		var target_file = path.get_file().to_lower()
		var dir = DirAccess.open(dir_path)
		if dir:
			dir.list_dir_begin()
			var fn = dir.get_next()
			while fn != "":
				if fn.to_lower() == target_file:
					path = dir_path.path_join(fn)
					file = FileAccess.open(path, FileAccess.READ)
					break
				fn = dir.get_next()
	
	if not file:
		var err = FileAccess.get_open_error()
		MULogger.error("[MUTextureLoader] FAILED to open %s: %d" % [path, err])
		return null
	
	# Read whole file
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	MULogger.info("Loading %s (%d bytes)" % [path.get_file(), buffer.size()])
	
	# Strategy: Try to detect if this is already a valid image (raw JPG/TGA)
	# by checking for common signatures BEFORE any decryption, 
	# BUT ONLY if it's not a known MU format
	var ext = path.get_extension().to_lower()
	var is_mu_format = ext in ["ozj", "ozt", "ozb", "map", "att"]
	
	var is_raw_jpg = buffer.size() >= 2 and buffer[0] == 0xFF and buffer[1] == 0xD8
	var is_raw_tga = MUDecryptor.is_tga(buffer)
	
	var data = buffer
	
	# If it's a raw image AND NOT a known MU format, don't skip ANY headers or decrypt
	if (is_raw_jpg or is_raw_tga) and not is_mu_format:
		if is_raw_jpg:
			# Even raw JPGs might have a double header
			if buffer.size() > 24 and buffer[24] == 0xFF and buffer[25] == 0xD8:
				print("    [MUTextureLoader] Detected DOUBLE-ENCODED JPG, skipping wrapper: ", 
					path.get_file())
				data = buffer.slice(24)
			else:
				print("    [MUTextureLoader] Detected RAW JPG (no wrapper): ", path.get_file())
				data = buffer
		else:
			print("    [MUTextureLoader] Detected RAW TGA: ", path.get_file())
			data = buffer
	else:
		# Encrypted files or files with headers - different header sizes for JPG vs TGA
		if ext == "ozj":
			# JPG files have 24-byte header
			print("    [MUTextureLoader] Processing OZJ: ", path.get_file())
			if buffer.size() > 24:
				data = buffer.slice(24)
				# Check if already a JPG after skip
				if not MUDecryptor.is_jpg(data):
					data = MUDecryptor.decrypt_texture(data)
			else:
				data = MUDecryptor.decrypt_texture(buffer)
		elif ext == "ozt":
			# TGA files have 4-byte header
			print("    [MUTextureLoader] Processing OZT: ", path.get_file())
			if buffer.size() > 4:
				data = buffer.slice(4)
				# Check if it produces a valid image after skip
				if not MUDecryptor.is_tga(data):
					# If not a TGA, try XOR decryption
					var xor_data = MUDecryptor.decrypt_texture(data)
					if MUDecryptor.is_tga(xor_data):
						data = xor_data
					else:
						# Final fallback: try raw buffer without skip if the 4-byte jump was wrong
						if MUDecryptor.is_tga(buffer):
							data = buffer
			else:
				data = buffer
		else:
			# Unknown format, try 24-byte header + decryption
			print("    [MUTextureLoader] Unknown format, trying default: ", path.get_file())
			if buffer.size() > 24:
				data = buffer.slice(24)
				data = MUDecryptor.decrypt_texture(data)
			else:
				data = MUDecryptor.decrypt_texture(buffer)
	
	# Load image
	var image = Image.new()
	var err = OK
	
	if ext == "ozt":
		# Try Godot's native TGA loader first (it's robust)
		if MUDecryptor.is_tga(data):
			err = image.load_tga_from_buffer(data)
		
		# If Godot fails (e.g. custom header) or we have OZT specifically, try custom decoder
		if err != OK:
			image = _decode_mu_tga(data)
			if image: err = OK
		
		# HEURISTIC: Many MU OZT files have 0 alpha for all pixels 
		# but are meant to be opaque (or color-keyed).
		if err == OK and image:
			_apply_alpha_heuristics(image)
			
	elif MUDecryptor.is_jpg(data):
		err = image.load_jpg_from_buffer(data)
	elif MUDecryptor.is_tga(data):
		err = image.load_tga_from_buffer(data)
		if err == OK:
			_apply_alpha_heuristics(image)
	else:
		err = ERR_INVALID_DATA
		
	if err == OK:
		print("    [MUTextureLoader] Successfully loaded image: ", path.get_file())
		return ImageTexture.create_from_image(image)
	
	push_error("[MUTextureLoader] FAILED to load image from buffer: %s error:%d" % [path, err])
	return null

## Custom decoder for MU TGA format (.OZT files)
## Receives the TGA data (header + pixels)
static func _decode_mu_tga(data: PackedByteArray) -> Image:
	if data.size() < 18:
		return null
	
	# TGA Header structure:
	# Offset 12: Width (int16)
	# Offset 14: Height (int16)
	# Offset 16: Bit depth (8, 16, 24, 32)
	# Offset 17: Descriptor (bit 5 = flip)
	
	var width = data[12] | (data[13] << 8)
	var height = data[14] | (data[15] << 8)
	var bit_depth = data[16]
	var descriptor = data[17]
	
	var index = 18 # Standard TGA header size
	
	# Skip ID field if present
	index += data[0]
	
	# Validate
	if bit_depth != 32 or width <= 0 or height <= 0 or width > 8192 or height > 8192:
		return null
	
	# Check we have enough data
	var expected_size = width * height * 4
	if data.size() - index < expected_size:
		return null
	
	# Convert BGRA to RGBA with vertical flip if bit 5 of descriptor is 0
	var flip = (descriptor & 0x20) == 0
	var pixel_data = PackedByteArray()
	pixel_data.resize(width * height * 4)
	
	for y in range(height):
		var dst_y = (height - 1 - y) if flip else y
		var dst_offset = dst_y * width * 4
		var src_offset = index + (y * width * 4)
		
		for x in range(width):
			var src_idx = src_offset + (x * 4)
			var dst_idx = dst_offset + (x * 4)
			
			# Convert BGRA to RGBA
			pixel_data[dst_idx + 0] = data[src_idx + 2]  # R = B
			pixel_data[dst_idx + 1] = data[src_idx + 1]  # G = G
			pixel_data[dst_idx + 2] = data[src_idx + 0]  # B = R
			pixel_data[dst_idx + 3] = data[src_idx + 3]  # A = A
	
	return Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, pixel_data)

## Heuristic to fix alpha channel in legacy MU textures
static func _apply_alpha_heuristics(image: Image):
	if image.get_format() != Image.FORMAT_RGBA8:
		return
		
	var has_alpha = false
	var all_transparent = true
	
	# Sample a subset of pixels for efficiency
	for y in range(0, image.get_height(), 4):
		for x in range(0, image.get_width(), 4):
			var pixel = image.get_pixel(x, y)
			if pixel.a > 0.05:
				all_transparent = false
			if pixel.a < 0.95:
				has_alpha = true
	
	# If it's 100% transparent, it's definitely a broken alpha channel (intended to be opaque)
	# or it's a "Color Keyed" texture where black is transparent.
	if all_transparent:
		print("    [MUTextureLoader] DETECTED BROKEN ALPHA (All Zero). Forcing to Opaque/Colorkey.")
		for y in range(image.get_height()):
			for x in range(image.get_width()):
				var pixel = image.get_pixel(x, y)
				# If color is not black, set alpha to 255
				# If color IS black, leave it transparent (Colorkey)
				if pixel.r > 0.05 or pixel.g > 0.05 or pixel.b > 0.05:
					pixel.a = 1.0
					image.set_pixel(x, y, pixel)
	elif not has_alpha:
		# If it's already 100% opaque, nothing to do
		pass
