class_name TextureConverterHeadless
const MUFileUtil = preload("res://addons/mu_tools/core/mu_file_util.gd")

## Headless Texture Converter
##
## Converts MuOnline .ozj/.ozt files to PNG without editor dependencies

const FORMAT_DXT1 = 1
const FORMAT_DXT3 = 3
const FORMAT_DXT5 = 5

func convert_directory(input_dir: String, output_dir: String) -> Dictionary:
	var result = {
		"total": 0,
		"success": 0,
		"failed": 0,
		"files": []
	}
	
	var dir = DirAccess.open(MUFileUtil.resolve_case(input_dir))
	if not dir:
		push_error("Failed to open directory: " + input_dir)
		return result
	
	dir.list_dir_begin()
	var file_name = dir.get_next()
	
	while file_name != "":
		if not dir.current_is_dir():
			var ext = file_name.get_extension().to_lower()
			if ext == "ozj" or ext == "ozt":
				result.total += 1
				var input_path = input_dir.path_join(file_name)
				var output_path = output_dir.path_join(file_name.get_basename() + ".png")
				
				print("[Converting] ", file_name, "...")
				if convert_file(input_path, output_path):
					result.success += 1
					result.files.append(output_path)
					print("  ✓ Success: ", output_path)
				else:
					result.failed += 1
					print("  ✗ Failed: ", file_name)
		
		file_name = dir.get_next()
	
	dir.list_dir_end()
	return result

func convert_file(input_path: String, output_path: String) -> bool:
	var file = MUFileUtil.open_file(input_path, FileAccess.READ)
	if not file: return false
	var raw_data = file.get_buffer(file.get_length())
	file.close()

	if raw_data.size() < 4: return false
	var image: Image = null

	# 1. Try MU Proprietary Header (24-byte header)
	var stream = StreamPeerBuffer.new()
	stream.data_array = raw_data
	var mu_header = _read_header_from_stream(stream)
	if not mu_header.is_empty():
		var pixel_data_raw = raw_data.slice(24)
		var decrypted_pix = MUDecryptor.decrypt_texture(pixel_data_raw)
		image = _load_image(mu_header, decrypted_pix)
		if image: return image.save_png(output_path) == OK

	# 2. Try Offset 24 (Dummy header + unencrypted data)
	if raw_data.size() > 24:
		var data_24 = raw_data.slice(24)
		image = _try_load_raw(data_24)
		if image: return image.save_png(output_path) == OK

	# 3. Try Raw Decrypted (No header, but XORed)
	var decrypted_raw = MUDecryptor.decrypt_texture(raw_data)
	image = _try_load_raw(decrypted_raw)
	if image: return image.save_png(output_path) == OK

	# 4. Try Raw Offset 0 (Last resort)
	image = _try_load_raw(raw_data)
	if image: return image.save_png(output_path) == OK

	return false

func _try_load_raw(data: PackedByteArray) -> Image:
	var img = Image.new()
	if MUDecryptor.is_jpg(data):
		if img.load_jpg_from_buffer(data) == OK: return img
	elif MUDecryptor.is_tga(data):
		if img.load_tga_from_buffer(data) == OK: return img
	return null

func _read_header_from_stream(stream: StreamPeerBuffer) -> Dictionary:
	var sig = stream.get_data(3)
	var sig_bytes = sig[1] as PackedByteArray
	var sig_str = sig_bytes.get_string_from_ascii()
	
	if sig_str != "OZJ" and sig_str != "OZT":
		return {}
		
	var header = {"signature": sig_str}
	stream.seek(8)
	header.width = stream.get_u32()
	header.height = stream.get_u32()
	header.format = stream.get_u32()
	return header

func _load_image(header: Dictionary, decrypted_data: PackedByteArray) -> Image:
	var image = Image.new()
	var err = OK
	
	if MUDecryptor.is_jpg(decrypted_data):
		err = image.load_jpg_from_buffer(decrypted_data)
	elif MUDecryptor.is_tga(decrypted_data):
		err = image.load_tga_from_buffer(decrypted_data)
	else:
		# Fallback to DXT
		image = _create_image_dxt(header, decrypted_data)
	
	if err != OK:
		push_error("Failed to load image buffer: " + str(err))
		return null
		
	return image

func _create_image_dxt(header: Dictionary, pixel_data: PackedByteArray) -> Image:
	var format_map = {
		FORMAT_DXT1: Image.FORMAT_DXT1,
		FORMAT_DXT3: Image.FORMAT_DXT3,
		FORMAT_DXT5: Image.FORMAT_DXT5
	}
	
	var godot_format = format_map.get(header.format, -1)
	if godot_format == -1:
		push_error("Unsupported format: " + str(header.format))
		return null
	
	var image = Image.create_from_data(
		header.width,
		header.height,
		false,
		godot_format,
		pixel_data
	)
	
	return image
