extends Node

class_name MUTerrainParser

const MUCoordinates = preload("res://addons/mu_tools/mu_coordinates.gd")

# XOR key for map and object files decryption
const MAP_XOR_KEY = [
	0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 
	0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
]

const TERRAIN_SIZE = 256

class MapData:
	var map_number: int
	var layer1: PackedByteArray # 256x256
	var layer2: PackedByteArray # 256x256
	var alpha: PackedFloat32Array # 256x256

class ObjectData:
	var type: int
	var position: Vector3
	var rotation: Vector3
	var scale: float

static func decrypt_map_file(data: PackedByteArray) -> PackedByteArray:
	var decrypted = PackedByteArray()
	decrypted.resize(data.size())
	
	var map_key: int = 0x5E
	for i in range(data.size()):
		var src_byte = data[i]
		var xor_byte = MAP_XOR_KEY[i % 16]
		
		# (pbySrc[i] ^ byMapXorKey[i % 16]) - (BYTE)wMapKey;
		var val = (src_byte ^ xor_byte) - map_key
		decrypted[i] = val & 0xFF
		
		# wMapKey = pbySrc[i] + 0x3D;
		map_key = (src_byte + 0x3D) & 0xFF
		
	return decrypted

func parse_height_file(path: String) -> PackedFloat32Array:
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		push_error("Cannot open height file: ", path)
		return PackedFloat32Array()
	
	# Skip BMP-style header (1080 bytes) + first 4 bytes
	# Reference: ZzzLodTerrain.cpp:663 fseek(fp, 4, SEEK_SET)
	file.seek(4)
	var header = file.get_buffer(1080)
	var raw_heights = file.get_buffer(TERRAIN_SIZE * TERRAIN_SIZE)
	
	var heights = PackedFloat32Array()
	heights.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	
	# Reference: ZzzLodTerrain.cpp:700
	# *dst = (float)(*src) * factor;
	# Lorencia uses factor 1.5, then we convert to Godot scale (/ 100.0)
	const HEIGHT_FACTOR = 1.5
	const SCALE = MUCoordinates.TERRAIN_SCALE
	
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		# Convert: byte_value * 1.5 (MU height) / 100.0 (to Godot meters)
		heights[i] = float(raw_heights[i]) * HEIGHT_FACTOR / SCALE
		
	return heights

func parse_mapping_file(path: String) -> MapData:
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		return null
		
	var encrypted_data = file.get_buffer(file.get_length())
	var data = decrypt_map_file(encrypted_data)
	
	var res = MapData.new()
	var ptr = 0
	
	# Skip version byte
	ptr += 1
	
	res.map_number = data[ptr]
	ptr += 1
	
	res.layer1 = data.slice(ptr, ptr + TERRAIN_SIZE * TERRAIN_SIZE)
	ptr += TERRAIN_SIZE * TERRAIN_SIZE
	
	res.layer2 = data.slice(ptr, ptr + TERRAIN_SIZE * TERRAIN_SIZE)
	ptr += TERRAIN_SIZE * TERRAIN_SIZE
	
	res.alpha.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		res.alpha[i] = float(data[ptr]) / 255.0
		ptr += 1
		
	return res

func parse_attributes_file(path: String) -> PackedByteArray:
	"""Parse terrain attributes (collision/walkability) from .att file"""
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		push_error("Cannot open attributes file: ", path)
		return PackedByteArray()
	
	var encrypted_data = file.get_buffer(file.get_length())
	var data = decrypt_map_file(encrypted_data)
	
	# File format:
	# Byte 0: Version
	# Byte 1: Map number
	# Byte 2: Width (256)
	# Byte 3: Height (256)
	# Byte 4+: Attribute data (BYTE or WORD format)
	
	# Check if BYTE or WORD format based on size
	var expected_byte_size = 4 + (TERRAIN_SIZE * TERRAIN_SIZE)
	var expected_word_size = 4 + (TERRAIN_SIZE * TERRAIN_SIZE * 2)
	
	var attributes = PackedByteArray()
	attributes.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	
	if data.size() == expected_byte_size:
		# BYTE format (legacy)
		for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
			attributes[i] = data[4 + i]
	elif data.size() == expected_word_size:
		# WORD format (extended) - use low byte only for simplicity
		for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
			attributes[i] = data[4 + i * 2]
	else:
		push_error("Invalid attributes file size: ", data.size())
		return PackedByteArray()
	
	print("[Terrain Parser] Loaded %d attribute bytes" % attributes.size())
	return attributes

func parse_objects_file(path: String) -> Array[ObjectData]:
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		return []
		
	var encrypted_data = file.get_buffer(file.get_length())
	var data = decrypt_map_file(encrypted_data)
	
	var objects: Array[ObjectData] = []
	var ptr = 0
	
	# Skip version/map number
	ptr += 2
	
	var count = data.decode_s16(ptr)
	ptr += 2
	
	print("[Terrain Parser] Loading %d objects from %s" % [count, path.get_file()])
	
	for i in range(count):
		var obj = ObjectData.new()
		obj.type = data.decode_s16(ptr)
		ptr += 2
		
		# Read MU world-space position (vec3_t)
		# Reference: ZzzObject.cpp:4954 memcpy(Position, Data + DataPtr, sizeof(vec3_t))
		var mu_pos = Vector3(
			data.decode_float(ptr),
			data.decode_float(ptr + 4),
			data.decode_float(ptr + 8)
		)
		ptr += 12
		
		# Read MU Euler angles (vec3_t)
		var mu_angle = Vector3(
			data.decode_float(ptr),
			data.decode_float(ptr + 4),
			data.decode_float(ptr + 8)
		)
		ptr += 12
		
		obj.scale = data.decode_float(ptr)
		ptr += 4
		
		# Convert using MUCoordinates utilities
		obj.position = MUCoordinates.mu_to_godot_position(mu_pos)
		obj.rotation = MUCoordinates.mu_angle_to_godot_rotation(mu_angle)
		
		objects.append(obj)
		
	return objects
