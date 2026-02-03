@tool
class_name MUTextureLoader

const MUDecryptor = preload("res://addons/mu_tools/mu_decryptor.gd")

## Directly load and decrypt an OZJ/OZT file as an ImageTexture
static func load_mu_texture(path: String) -> ImageTexture:
	if not FileAccess.file_exists(path):
		return null
		
	var file = FileAccess.open(path, FileAccess.READ)
	if not file: return null
	
	# Skip MU header (24 bytes)
	file.seek(24)
	var encrypted = file.get_buffer(file.get_length() - 24)
	file.close()
	
	# Decrypt
	var decrypted = MUDecryptor.decrypt_texture(encrypted)
	
	# Load image
	var image = Image.new()
	var err = OK
	if MUDecryptor.is_jpg(decrypted):
		err = image.load_jpg_from_buffer(decrypted)
	elif MUDecryptor.is_tga(decrypted):
		err = image.load_tga_from_buffer(decrypted)
	else:
		err = ERR_INVALID_DATA
		
	if err == OK:
		return ImageTexture.create_from_image(image)
	return null
