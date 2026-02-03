@tool
class_name MUDecryptor

## MU Online Decryption Utility (Phase 0)
##
## Handles XOR and Cyclic keys used in MuOnline clients.
## Based on MapFile and Bmd_Decrypt logic in ZzzBMD.cpp.

const XOR_KEY_BMD = [0x2F, 0x52, 0x4D, 0x51, 0x34] # Example key, often varies
const XOR_KEY_TEXTURE = 0x5E

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
static func decrypt_texture(buffer: PackedByteArray) -> PackedByteArray:
	# Texture files often use a simpler XOR or fixed header shift
	var decrypted = buffer.duplicate()
	
	# Skip the header if already processed, but MuMain often XORs 
	# the entire block after the 24-byte header
	for i in range(decrypted.size()):
		decrypted[i] = decrypted[i] ^ XOR_KEY_TEXTURE
		
	return decrypted

## Detects if a buffer is a JPG after decryption
static func is_jpg(buffer: PackedByteArray) -> bool:
	if buffer.size() < 2: return false
	return buffer[0] == 0xFF and buffer[1] == 0xD8

## Detects if a buffer is a TGA after decryption
static func is_tga(buffer: PackedByteArray) -> bool:
	if buffer.size() < 18: return false
	
	# TGA footer is 26 bytes, but the signature "TRUEVISION-XFILE." is at offset -18
	var footer_sig = buffer.slice(buffer.size() - 18, buffer.size()).get_string_from_ascii()
	if footer_sig == "TRUEVISION-XFILE.":
		return true
		
	# Fallback: Check for common TGA header (offset 2 in decrypted data should be 2, 3, 10 or 11)
	# Header: [ID length, color map type, image type, ...]
	var image_type = buffer[2]
	return image_type in [2, 3, 10, 11]
