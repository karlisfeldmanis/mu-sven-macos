extends SceneTree

func _init():
	var path = "raw_data/Player/Player.bmd"
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		print("Failed to open")
		quit()
		return
		
	var raw_data = file.get_buffer(file.get_length())
	file.close()
	
	print("File size: ", raw_data.size())
	var version = raw_data[3]
	print("Version: ", version)
	
	var data_offset = 8
	var decrypted = decrypt(raw_data, data_offset)
	
	var stream = StreamPeerBuffer.new()
	stream.data_array = decrypted
	stream.seek(data_offset)
	
	var name = stream.get_data(32)[1].get_string_from_ascii()
	print("Model Name: ", name)
	
	var mesh_count = stream.get_u16()
	var bone_count = stream.get_u16()
	var action_count = stream.get_u16()
	
	print("Meshes: ", mesh_count)
	print("Bones: ", bone_count)
	print("Actions: ", action_count)
	
	for i in range(mesh_count):
		var nv = stream.get_u16()
		var nn = stream.get_u16()
		var nt = stream.get_u16()
		var ntri = stream.get_u16()
		var tex = stream.get_u16()
		print("Mesh ", i, ": V:", nv, " N:", nn, " T:", nt, " Tri:", ntri, " Tex:", tex)
		
		# Skip Vertices
		stream.seek(stream.get_position() + nv * 14)
		# Skip Normals
		stream.seek(stream.get_position() + nn * 16)
		# Skip UVs
		stream.seek(stream.get_position() + nt * 8)
		
		# Check Triangles
		var tri_start = stream.get_position()
		print("Triangles start at: ", tri_start)
		# Peek at first triangle
		var poly = stream.get_8()
		print("  Tri 0 Polygon: ", poly)
		stream.seek(tri_start + 59) # Based on my calc
		var next_poly = stream.get_8()
		print("  Tri 1 Polygon: ", next_poly)
		
		# Try to find next mesh by texture name
		# Meshes are followed by 32 bytes texture name
		
	quit()

func decrypt(buffer: PackedByteArray, start: int) -> PackedByteArray:
	var d = buffer.duplicate()
	var key = 0x5E
	for i in range(start, d.size()):
		d[i] = d[i] ^ key
		key = (d[i] + 0x3D) & 0xFF
	return d
