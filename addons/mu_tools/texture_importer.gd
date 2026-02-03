@tool
extends EditorImportPlugin

## MU Texture Importer (Phase 1)
##
## Imports MuOnline .ozj and .ozt texture files by:
## 1. Stripping the 24-byte proprietary header
## 2. Decompressing DXT-compressed data
## 3. Converting to Godot-compatible Image format
##
## Based on ZzzTexture.cpp from Sven-n/MuMain

enum Presets { DEFAULT }

## Texture format constants (from MuMain)
const FORMAT_DXT1 = 1
const FORMAT_DXT3 = 3
const FORMAT_DXT5 = 5

func _get_importer_name() -> String:
	return "mu.texture"

func _get_visible_name() -> String:
	return "MU Texture"

func _get_recognized_extensions() -> PackedStringArray:
	return PackedStringArray(["ozj", "ozt"])

func _get_save_extension() -> String:
	return "ctex"  # Godot's compressed texture format

func _get_resource_type() -> String:
	return "CompressedTexture2D"

func _get_preset_count() -> int:
	return Presets.size()

func _get_preset_name(preset_index: int) -> String:
	match preset_index:
		Presets.DEFAULT:
			return "Default"
		_:
			return "Unknown"

func _get_import_options(path: String, preset_index: int) -> Array[Dictionary]:
	return [
		{
			"name": "compress/mode",
			"default_value": 0,  # VRAM Compressed
			"property_hint": PROPERTY_HINT_ENUM,
			"hint_string": "VRAM Compressed,Lossless,Lossy"
		},
		{
			"name": "mipmaps/generate",
			"default_value": true
		},
		{
			"name": "debug/log_details",
			"default_value": false
		}
	]

func _get_option_visibility(path: String, option_name: StringName, options: Dictionary) -> bool:
	return true

func _get_priority() -> float:
	return 1.0

func _get_import_order() -> int:
	return 0

func _import(source_file: String, save_path: String, options: Dictionary, 
			platform_variants: Array[String], gen_files: Array[String]) -> Error:
	
	var debug_log = options.get("debug/log_details", false)
	
	if debug_log:
		print("[MU Texture] Importing: ", source_file)
	
	# Open the source file
	var file = FileAccess.open(source_file, FileAccess.READ)
	if not file:
		push_error("[MU Texture] Failed to open file: " + source_file)
		return ERR_FILE_CANT_OPEN
	
	# Ensure Little-Endian byte order (MuOnline standard)
	file.big_endian = false
	
	# Read and validate header
	var header = _read_header(file, debug_log)
	if header.is_empty():
		file.close()
		return ERR_FILE_CORRUPT
	
	# Read and decrypt pixel data
	var raw_data = file.get_buffer(file.get_length() - file.get_position())
	file.close()
	
	var decrypted_data = MUDecryptor.decrypt_texture(raw_data)
	
	# Detect format and load image
	var image = Image.new()
	var err = OK
	
	if MUDecryptor.is_jpg(decrypted_data):
		err = image.load_jpg_from_buffer(decrypted_data)
	elif MUDecryptor.is_tga(decrypted_data):
		err = image.load_tga_from_buffer(decrypted_data)
	else:
		# Fallback to DXT if dimensions are power of two
		image = _create_image_dxt(header, decrypted_data, debug_log)
		if not image:
			return ERR_INVALID_DATA
	
	if err != OK:
		push_error("[MU Texture] Failed to load image buffer: ", err)
		return err
	
	# Apply import options
	if options.get("mipmaps/generate", true):
		image.generate_mipmaps()
	
	# Create and save texture resource
	var texture = ImageTexture.create_from_image(image)
	
	# Save as material if requested (MuOnline uses custom materials)
	var material = MUMaterialFactory.create_material(texture, header.get("flags", 0))
	
	var filename = save_path + "." + _get_save_extension()
	err = ResourceSaver.save(texture, filename)
	
	if err != OK:
		push_error("[MU Texture] Failed to save texture: " + filename)
		return err
	
	if debug_log:
		print("[MU Texture] Successfully imported: ", source_file)
	
	return OK

func _create_image_dxt(header: Dictionary, pixel_data: PackedByteArray, debug: bool) -> Image:
	var format_map = {
		FORMAT_DXT1: Image.FORMAT_DXT1,
		FORMAT_DXT3: Image.FORMAT_DXT3,
		FORMAT_DXT5: Image.FORMAT_DXT5
	}
	
	var godot_format = format_map.get(header.format, -1)
	if godot_format == -1:
		push_error("[MU Texture] Unsupported DXT format: ", header.format)
		return null
	
	# Validate DXT size
	var expected_size = _calculate_dxt_size(header.width, header.height, header.format)
	if pixel_data.size() < expected_size:
		push_error("[MU Texture] DXT buffer underflow")
		return null
	
	var image = Image.create_from_data(
		header.width,
		header.height,
		false,
		godot_format,
		pixel_data
	)
	
	return image

## Reads the 24-byte MuOnline texture header
func _read_header(file: FileAccess, debug: bool) -> Dictionary:
	if file.get_length() < 24:
		push_error("[MU Texture] File too small to contain valid header")
		return {}
	
	var header = {}
	
	# Read signature (4 bytes)
	var signature = file.get_buffer(4)
	var sig_str = signature.get_string_from_ascii()
	
	if not (sig_str.begins_with("OZJ") or sig_str.begins_with("OZT")):
		push_error("[MU Texture] Invalid signature: " + sig_str)
		return {}
	
	header.signature = sig_str
	
	# Skip unknown field (4 bytes)
	file.get_32()
	
	# Read dimensions and format (12 bytes)
	header.width = file.get_32()
	header.height = file.get_32()
	header.format = file.get_32()
	header.mipmap_count = file.get_32()
	
	# Validate dimensions
	if header.width == 0 or header.height == 0 or header.width > 4096 or header.height > 4096:
		push_error("[MU Texture] Invalid dimensions: %dx%d" % [header.width, header.height])
		return {}
	
	if debug:
		print("[MU Texture] Header parsed:")
		print("  Signature: ", header.signature)
		print("  Dimensions: ", header.width, "x", header.height)
		print("  Format: ", _format_to_string(header.format))
		print("  Mipmaps: ", header.mipmap_count)
	
	return header

## Reads DXT-compressed pixel data
func _read_pixel_data(file: FileAccess, header: Dictionary, debug: bool) -> PackedByteArray:
	var data_size = _calculate_dxt_size(header.width, header.height, header.format)
	
	if debug:
		print("[MU Texture] Reading ", data_size, " bytes of pixel data")
	
	var remaining = file.get_length() - file.get_position()
	if remaining < data_size:
		push_error("[MU Texture] Not enough data. Expected: %d, Available: %d" % [data_size, remaining])
		return PackedByteArray()
	
	return file.get_buffer(data_size)

## Calculates the size of DXT-compressed data
func _calculate_dxt_size(width: int, height: int, format: int) -> int:
	var block_size = 16 if format == FORMAT_DXT5 or format == FORMAT_DXT3 else 8
	var blocks_x = max(1, (width + 3) / 4)
	var blocks_y = max(1, (height + 3) / 4)
	return blocks_x * blocks_y * block_size

## Creates a Godot Image from DXT data
func _create_image(header: Dictionary, pixel_data: PackedByteArray, debug: bool) -> Image:
	var format_map = {
		FORMAT_DXT1: Image.FORMAT_DXT1,
		FORMAT_DXT3: Image.FORMAT_DXT3,
		FORMAT_DXT5: Image.FORMAT_DXT5
	}
	
	var godot_format = format_map.get(header.format, -1)
	if godot_format == -1:
		push_error("[MU Texture] Unsupported format: ", header.format)
		return null
	
	var image = Image.create_from_data(
		header.width,
		header.height,
		false,  # No mipmaps in raw data
		godot_format,
		pixel_data
	)
	
	if not image:
		push_error("[MU Texture] Failed to create image from DXT data")
		return null
	
	return image

## Helper function to convert format ID to string
func _format_to_string(format: int) -> String:
	match format:
		FORMAT_DXT1:
			return "DXT1"
		FORMAT_DXT3:
			return "DXT3"
		FORMAT_DXT5:
			return "DXT5"
		_:
			return "Unknown (%d)" % format
