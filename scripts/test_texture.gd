extends SceneTree

func _init():
	var path = "raw_data/Player/HQlevel_man033.ozj"
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		print("Failed to open")
		quit()
		return
		
	var data = file.get_buffer(file.get_length())
	file.close()
	
	print("File size: ", data.size())
	print("First 16 bytes: ", data.slice(0, 16).hex_encode())
	print("Bytes at 24: ", data.slice(24, 40).hex_encode())
	
	var strategies = [
		{"name": "Raw", "data": data},
		{"name": "Offset 24", "data": data.slice(24)},
		{"name": "Decrypted Raw", "data": decrypt(data)},
		{"name": "Decrypted Offset 24", "data": decrypt(data.slice(24))}
	]
	
	for s in strategies:
		var img = Image.new()
		var err = img.load_jpg_from_buffer(s.data)
		if err == OK:
			print("Strategy [", s.name, "] SUCCESS! Size: ", img.get_size())
			img.save_png("test_" + s.name.replace(" ", "_") + ".png")
		else:
			print("Strategy [", s.name, "] FAILED: ", err)
			
	quit()

func decrypt(buffer: PackedByteArray) -> PackedByteArray:
	var d = buffer.duplicate()
	for i in range(d.size()):
		d[i] = d[i] ^ 0x5E
	return d
