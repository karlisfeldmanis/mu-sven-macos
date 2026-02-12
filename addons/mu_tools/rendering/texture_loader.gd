@tool
# class_name MUTextureLoader

const MUDecryptor = preload("res://addons/mu_tools/core/mu_decryptor.gd")
const MULogger = preload("res://addons/mu_tools/util/mu_logger.gd")
const MUFileUtil = preload("res://addons/mu_tools/core/file_util.gd")

## Directly load and decrypt an OZJ/OZT file as an ImageTexture
static func load_mu_texture(path: String) -> ImageTexture:
	# Proactively resolve case to avoid Godot warnings on macOS/Windows 
	# and failures on Linux.
	var actual_path = MUFileUtil.resolve_case(path)
	
	var file = FileAccess.open(actual_path, FileAccess.READ)
	if not file:
		var err = FileAccess.get_open_error()
		MULogger.error("[MUTextureLoader] FAILED to open %s: %d" % [actual_path, err])
		return null
	
	# Read whole file
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	if buffer.is_empty():
		return null
		
	var ext = path.get_extension().to_upper()
	var img_data = buffer
	
	# SVEN Parity: Strict Header Skips
	# In SVEN (Main 5.2), OpenJpeg and OpenTga always skip fixed header sizes
	# regardless of content, as they are custom wrapped JPG/TGA.
	if ext == "OZJ":
		if buffer.size() > 24:
			img_data = buffer.slice(24)
	elif ext == "OZT" or ext == "OZB":
		if buffer.size() > 4:
			img_data = buffer.slice(4)
	
	# Decrypt if needed (OZJ/OZT usually need XOR unless already raw JPG/TGA)
	if not _is_raw_jpg(img_data) and not _is_raw_tga(img_data):
		img_data = MUDecryptor.decrypt_texture(img_data, ext)
	
	# Load image
	var image = Image.new()
	var err = ERR_INVALID_DATA
	
	if ext == "OZT" or ext == "OZB" or _is_raw_tga(img_data):
		# Try Godot's native TGA loader first (it supports 24/32-bit but not all flips)
		err = image.load_tga_from_buffer(img_data)
		# If Godot fails, use our custom decoder which is more robust for MU's BGRA TGAs
		if err != OK:
			image = _decode_mu_tga(img_data)
			if image: err = OK
	else:
		# JPG / OZJ
		err = image.load_jpg_from_buffer(img_data)
	
	if err != OK:
		push_error("[MUTextureLoader] FAILED to load %s (Error: %d). Data size: %d" % 
				[path.get_file(), err, img_data.size()])
		return null
	# Apply common MU heuristics (Alpha channel corrections)
	_apply_alpha_heuristics(image, path)
	
	var texture = ImageTexture.create_from_image(image)
	return texture

static func _is_raw_jpg(buffer: PackedByteArray) -> bool:
	return buffer.size() >= 2 and buffer[0] == 0xFF and buffer[1] == 0xD8

static func _is_raw_tga(buffer: PackedByteArray) -> bool:
	return MUDecryptor.is_tga(buffer)

static func _apply_alpha_heuristics(image: Image, path: String):
	var lower_name = path.to_lower()
	
	# 1. Black Color-Key Transparency
	# Many older MU assets use pure black (0,0,0) as transparency.
	# We only apply this to images that aren't terrain tiles (terrain blended in shader)
	# and don't already have a meaningful alpha channel (e.g. 32-bit bird textures).
	if (lower_name.contains("/object") or lower_name.contains("/player")) and not lower_name.contains("bird"):
		# If the image already has some transparency, don't force black-keying
		if image.detect_alpha() == Image.ALPHA_NONE:
			image.convert(Image.FORMAT_RGBA8)
			for y in range(image.get_height()):
				for x in range(image.get_width()):
					var c = image.get_pixel(x, y)
					if c.r < 0.01 and c.g < 0.01 and c.b < 0.01:
						image.set_pixel(x, y, Color(0, 0, 0, 0))
					
	# 2. Binary Alpha Fix
	# Some OZT files have broken 1-bit alpha that Godot treats as white.
	# If an image is purely white/transparent, we might need a fix.

static func _decode_mu_tga(buffer: PackedByteArray) -> Image:
	if buffer.size() < 18: return null
	
	var width = buffer[12] | (buffer[13] << 8)
	var height = buffer[14] | (buffer[15] << 8)
	var depth = buffer[16]
	var descriptor = buffer[17]
	
	var img = Image.create(width, height, false, Image.FORMAT_RGBA8)
	var data_ptr = 18
	
	if depth == 32:
		# BGRA -> RGBA + Vertical Flip
		for y in range(height):
			# MU TGAs are often bottom-up (descriptor bit 5 is 0)
			var target_y = height - 1 - y if not (descriptor & 0x20) else y
			for x in range(width):
				if data_ptr + 3 < buffer.size():
					var b = float(buffer[data_ptr]) / 255.0
					var g = float(buffer[data_ptr + 1]) / 255.0
					var r = float(buffer[data_ptr + 2]) / 255.0
					var a = float(buffer[data_ptr + 3]) / 255.0
					img.set_pixel(x, target_y, Color(r, g, b, a))
					data_ptr += 4
		return img
		
	if depth == 24:
		# BGR -> RGB + Vertical Flip
		for y in range(height):
			var target_y = height - 1 - y if not (descriptor & 0x20) else y
			for x in range(width):
				if data_ptr + 2 < buffer.size():
					var b = float(buffer[data_ptr]) / 255.0
					var g = float(buffer[data_ptr + 1]) / 255.0
					var r = float(buffer[data_ptr + 2]) / 255.0
					img.set_pixel(x, target_y, Color(r, g, b, 1.0))
					data_ptr += 3
		return img
		
	return null
