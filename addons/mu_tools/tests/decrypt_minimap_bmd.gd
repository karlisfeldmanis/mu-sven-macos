@tool
extends SceneTree

func _init():
	print("\n======================================================================")
	print("MINIMAP BMD DECRYPTOR (Final Version)")
	print("======================================================================")
	
	var world_id = 1
	var bmd_path = "res://reference/MuMain/src/bin/Data/World%d/Minimap.bmd" % world_id
	
	var file = FileAccess.open(bmd_path, FileAccess.READ)
	if not file:
		print("FAILED to open Minimap.bmd")
		quit()
		return
		
	var buffer = file.get_buffer(file.get_length())
	file.close()
	
	var keys = [0xFC, 0xCF, 0xAB]
	var max_entries = 100
	var name_len = 100
	var entry_size = 116 # 1+8+4+100 = 113, padded to 116.
	
	print("Data Size: %d" % buffer.size())
	
	var decrypted = PackedByteArray()
	decrypted.resize(max_entries * entry_size)
	
	for i in range(max_entries):
		var entry_start = i * entry_size
		for j in range(entry_size):
			var global_idx = entry_start + j
			if global_idx >= buffer.size(): break
			decrypted[global_idx] = buffer[global_idx] ^ keys[j % 3]
			
	var stream = StreamPeerBuffer.new()
	stream.data_array = decrypted
	stream.big_endian = true # Records are Big Endian in this BMD version
	
	print("\nEntries found:")
	for i in range(max_entries):
		stream.seek(i * entry_size)
		
		var kind = stream.get_8()
		var lx = stream.get_32()
		var ly = stream.get_32()
		var rot = stream.get_32()
		var name_data = stream.get_data(name_len)[1] as PackedByteArray
		
		if kind > 0:
			var name = ""
			var null_pos = name_data.find(0)
			if null_pos != -1:
				name = name_data.slice(0, null_pos).get_string_from_ascii().strip_edges()
			else:
				name = name_data.get_string_from_ascii().strip_edges()
				
			print("Entry %d: Kind=%d, Loc=(%d,%d), Rot=%d, Name='%s'" % [
				i, kind, lx, ly, rot, name
			])
		
	quit()
