@tool
# class_name MUDecryptor

## MU Online Decryption Utility (Phase 0)
##
## Handles XOR and Cyclic keys used in MuOnline clients.
## Based on MapFile and Bmd_Decrypt logic in ZzzBMD.cpp.

const XOR_KEY_BMD = [0x2F, 0x52, 0x4D, 0x51, 0x34]
const XOR_KEY_OZJ = 0x54 # Strict Project Parity
const XOR_KEY_OZT = 0x5E # Strict Project Parity
const XOR_KEY_OZB = 0x5E

## Decrypts a BMD buffer starting from an offset (Version 12 / MapFileDecrypt)
static func decrypt_bmd_v12(buffer: PackedByteArray, start_offset: int) -> PackedByteArray:
	var encrypted_size = buffer.size() - start_offset
	var decrypted = PackedByteArray()
	decrypted.resize(encrypted_size)
	
	var xor_keys = [
		0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
		0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
	]
	
	var map_key: int = 0x5E
	for i in range(encrypted_size):
		var src_byte = buffer[start_offset + i]
		decrypted[i] = (src_byte ^ xor_keys[i % 16]) - (map_key & 0xFF)
		map_key = (src_byte + 0x3D) & 0xFF
		
	return decrypted

## Decrypts a BMD buffer starting from an offset (Version 10 / Legacy XOR)
static func decrypt_bmd_v10(buffer: PackedByteArray, start_offset: int) -> PackedByteArray:
	var decrypted = buffer.slice(start_offset)
	var key: int = 0x5E
	
	for i in range(decrypted.size()):
		var original_byte = decrypted[i]
		decrypted[i] = decrypted[i] ^ key
		key = (decrypted[i] + 0x3D) & 0xFF
		
	return decrypted

# Helper to route decryption based on version
static func decrypt_bmd(buffer: PackedByteArray, version: int, 
		start_offset: int) -> PackedByteArray:
	if version == 0x0C:
		return decrypt_bmd_v12(buffer, start_offset)
	return decrypt_bmd_v10(buffer, start_offset)

## Decrypts texture files (.ozj, .ozt)
static func decrypt_texture(buffer: PackedByteArray, type_ext: String = "") -> PackedByteArray:
	# 1. If it's already a valid image, don't decrypt
	if is_jpg(buffer) or is_tga(buffer):
		return buffer
		
	# 2. Select key based on extension
	var key = 0x5E # Default
	var ext = type_ext.to_upper()
	if ext == "OZJ": key = XOR_KEY_OZJ
	elif ext == "OZT" or ext == "OZB": key = XOR_KEY_OZT
	
	# 3. Try XORing
	var decrypted = buffer.duplicate()
	for i in range(decrypted.size()):
		decrypted[i] = decrypted[i] ^ key
		
	return decrypted

## Detects if a buffer is a JPG after decryption
static func is_jpg(buffer: PackedByteArray) -> bool:
	if buffer.size() < 2: return false
	return buffer[0] == 0xFF and buffer[1] == 0xD8

## Detects if a buffer is a TGA after decryption
static func is_tga(buffer: PackedByteArray) -> bool:
	if buffer.size() < 18:
		return false
	
	# Common TGA headers
	# 0: id length (any)
	# 1: color map type (0 or 1)
	# 2: image type (2, 3, 10, 11)
	var image_type = buffer[2]
	if not image_type in [2, 3, 10, 11]:
		return false
	
	# Check for bit depth (offset 16)
	var bit_depth = buffer[16]
	if not bit_depth in [8, 16, 24, 32]:
		return false
	
	# Check for Width/Height > 0 (offsets 12, 14)
	var width = buffer[12] | (buffer[13] << 8)
	var height = buffer[14] | (buffer[15] << 8)
	if width <= 0 or height <= 0 or width > 8192 or height > 8192:
		return false
	
	# MU TGAs often don't have the "TRUEVISION-XFILE." footer if they are old
	# But if they do, it's a strong signal.
	if buffer.size() > 26:
		var footer_sig = buffer.slice(buffer.size() - 18).get_string_from_ascii()
		if footer_sig.contains("TRUEVISION-XFILE."):
			return true
	
	# For MU, we'll trust the image_type + bit_depth check if footer is missing
	return true
