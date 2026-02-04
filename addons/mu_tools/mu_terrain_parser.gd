extends Node

class_name MUTerrainParser

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
	var water_tiles: Array # Positions of water tiles (Vector2i) - index 5
	var grass_tiles: Array # Positions of grass tiles (Vector2i) - indices 0-2

class ObjectData:
	var type: int
	var position: Vector3
	var rotation: Quaternion
	var scale: float
	var mu_euler: Vector3 # Raw decidegrees
	var mu_pos_raw: Vector3 # Raw MU world units
	var hidden_mesh: int = -1  # -2 = hidden (SVEN: HiddenMesh), -1 = visible

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
	const TERRAIN_SCALE = 100.0  # MU units to Godot meters
	
	# SVEN stores as [y][x], we map this directly to Godot (x, -y)
	for i in range(raw_heights.size()):
		heights[i] = float(raw_heights[i]) * HEIGHT_FACTOR / TERRAIN_SCALE
		
	return heights

func parse_mapping_file(path: String) -> MapData:
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		return null
		
	var encrypted_data = file.get_buffer(file.get_length())
	var data = decrypt_map_file(encrypted_data)
	print("[Terrain Parser] Decrypted Map Data (first 16 bytes): ", data.slice(0, 16))
	
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
	
	# Load Alpha Map (Indices 2 * 64k onwards)
	# Alpha is 1 byte per tile, stored directly after Layer 1 and Layer 2
	res.alpha.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		res.alpha[i] = float(data[ptr]) / 255.0
		ptr += 1
	
	# Detect water tiles (Layer1 index 5)
	res.water_tiles = []
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		if res.layer1[i] == 5:
			var x = i % TERRAIN_SIZE
			var y = i / TERRAIN_SIZE
			res.water_tiles.append(Vector2i(x, y))
	
	if not res.water_tiles.is_empty():
		print("[Terrain Parser] Found %d water tiles" % res.water_tiles.size())
	
	# Detect grass tiles (Layer1 indices 0-2)
	res.grass_tiles = []
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		if res.layer1[i] >= 0 and res.layer1[i] <= 2:
			var x = i % TERRAIN_SIZE
			var y = i / TERRAIN_SIZE
			res.grass_tiles.append(Vector2i(x, y))
	
	if not res.grass_tiles.is_empty():
		print("[Terrain Parser] Found %d grass tiles" % res.grass_tiles.size())
		
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
		# MU terrain objects store rotation in decidegrees (0-3600)
		var mu_angle = Vector3(
			data.decode_float(ptr),
			data.decode_float(ptr + 4),
			data.decode_float(ptr + 8)
		)
		ptr += 12
		
		obj.scale = data.decode_float(ptr)
		ptr += 4
		
		# Convert using MUCoordinateUtils (Standard Mapping)
		var mu_rad = Vector3(deg_to_rad(mu_angle.x), deg_to_rad(mu_angle.y), deg_to_rad(mu_angle.z))
		obj.position = MUCoordinateUtils.convert_object_position(mu_pos)
		obj.rotation = MUCoordinateUtils.convert_object_rotation(mu_rad)
		obj.mu_euler = mu_angle
		obj.mu_pos_raw = mu_pos
		
		# SVEN compatibility: Set HiddenMesh for invisible objects
		# MODEL_POSE_BOX = 133 (interactive pose trigger, invisible)
		if obj.type == 133:  # MODEL_POSE_BOX
			obj.hidden_mesh = -2
		
		# Diagnostic logging for rotation issues
		if i < 3000:
			print("  [Object Parser] Obj %d Type=%d Rot=%.2f,%.2f,%.2f Scale=%.2f" % [
				i, obj.type, mu_angle.x, mu_angle.y, mu_angle.z, obj.scale
			])
		
		objects.append(obj)
		
	return objects
