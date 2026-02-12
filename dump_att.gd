extends SceneTree

func _init():
	var parser = preload("res://addons/mu_tools/parsers/terrain_parser.gd").new()
	var file = FileAccess.open("reference/MuMain/src/bin/Data/World1/Terrain1.att", FileAccess.READ)
	if file:
		var raw = file.get_buffer(file.get_length())
		var dec = parser.decrypt_map_file(raw)
		var bux = [0xFC, 0xCF, 0xAB]
		for i in range(dec.size()):
			dec[i] ^= bux[i % 3]
		
		# Skip header (4 bytes)
		var counts = {}
		for i in range(256*256):
			var word = dec.decode_u16(4 + i * 2)
			var high = word >> 8
			if not counts.has(high): counts[high] = 0
			counts[high] += 1
		
		print("High Byte Distribution (Potential Rotation/Sym):")
		for k in counts:
			print("  %02X: %d" % [k, counts[k]])
	else:
		print("Failed to open attributes file")
	quit()
