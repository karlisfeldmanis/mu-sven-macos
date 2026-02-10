extends SceneTree

func _init():
	var parser = preload("res://addons/mu_tools/core/mu_terrain_parser.gd").new()
	var file = FileAccess.open("reference/MuMain/src/bin/Data/World1/EncTerrain1.map", FileAccess.READ)
	if file:
		var raw = file.get_buffer(file.get_length())
		var data = parser.decrypt_map_file(raw)
		
		var expected_total = 256*256 * 3
		var ptr = data.size() - expected_total
		
		var l1 = data.slice(ptr, ptr + 65536)
		var l2 = data.slice(ptr + 65536, ptr + 131072)
		
		var u1 = {}; var u2 = {}
		for b in l1:
			if not u1.has(b): u1[b] = 0
			u1[b] += 1
		for b in l2:
			if not u2.has(b): u2[b] = 0
			u2[b] += 1
			
		print("Unique Layer 1 (Hex):")
		for k in u1: print("  %02X: %d" % [k, u1[k]])
		
		print("Unique Layer 2 (Hex):")
		for k in u2: print("  %02X: %d" % [k, u2[k]])
	quit()
