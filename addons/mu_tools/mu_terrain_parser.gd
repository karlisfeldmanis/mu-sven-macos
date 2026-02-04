extends Node

class_name MUTerrainParser
const MUFileUtil = preload("res://addons/mu_tools/mu_file_util.gd")

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
	var grass_tiles: Dictionary # { type: [Vector2i, ...], ... }

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
	var file = MUFileUtil.open_file(path, FileAccess.READ)
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
	
	# SVEN stores as [y][x] where Y is "bottom-up" (BMP style)
	# based on SaveTerrainHeight reversed logic?
	# ZzzLodTerrain.cpp SaveTerrainHeight: writes Source[i] to Dest[(255-i)].
	# This implies the file is stored FLIPPED on Y.
	# OpenTerrainHeight reads it linearly. So Buffer[0] is Row 0 of File.
	# If File is Flipped, Row 0 is the "Bottom" (or Top?) of the map.
	# Let's try flipping the row index: file_row `y` -> map_row `255 - y`
	
	for y in range(TERRAIN_SIZE):
		for x in range(TERRAIN_SIZE):
			var file_idx = y * TERRAIN_SIZE + x
			# Direct Mapping: Load linearly
			# Row 0 in file = Row 0 in memory = X 0 in World
			# Add 5.0m baseline: SVEN terrain baseline is at -500 units (-5m)
			heights[file_idx] = float(raw_heights[file_idx]) * HEIGHT_FACTOR / TERRAIN_SCALE
		
	return heights

func parse_mapping_file(path: String) -> MapData:
	var file = MUFileUtil.open_file(path, FileAccess.READ)
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
	
	# Layer 1 & 2 Loading with Vertical Flip
	# Since we flipped the heightmap (Y -> 255-Y), we MUST flip the texture/splat maps too
	# to keep them aligned. Data is linear [Row0, Row1...] but Row0 is "Bottom".
	
	res.layer1.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	res.layer2.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	res.alpha.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	
	var raw_layer1 = data.slice(ptr, ptr + TERRAIN_SIZE * TERRAIN_SIZE)
	ptr += TERRAIN_SIZE * TERRAIN_SIZE
	
	var raw_layer2 = data.slice(ptr, ptr + TERRAIN_SIZE * TERRAIN_SIZE)
	ptr += TERRAIN_SIZE * TERRAIN_SIZE
	
	# Mapping for Alpha: stored after layers
	var raw_alpha_start = ptr
	
	for y in range(TERRAIN_SIZE):
		for x in range(TERRAIN_SIZE):
			var idx = y * TERRAIN_SIZE + x
			
			# Direct Mapping: No flip
			res.layer1[idx] = raw_layer1[idx]
			res.layer2[idx] = raw_layer2[idx]
			res.alpha[idx] = float(data[raw_alpha_start + idx]) / 255.0

	ptr += TERRAIN_SIZE * TERRAIN_SIZE # Advance past alpha block

	# Detect water tiles (Layer1 index 5)
	res.water_tiles = []
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		if res.layer1[i] == 5:
			var x = i % TERRAIN_SIZE
			var y = i / TERRAIN_SIZE
			# Note: 'i' here iterates the ALREADY FLIPPED buffer 'res.layer1'
			# So (x,y) are correct map coordinates (Godot X, Godot Z)
			# Ensure we don't spawn water on top of blended edges if alpha > 0.5
			if res.alpha[i] < 0.5:
				res.water_tiles.append(Vector2i(x, y))
	
	if not res.water_tiles.is_empty():
		print("[Terrain Parser] Found %d water tiles" % res.water_tiles.size())
	
	# Detect grass tiles (Layer1 indices 0-2)
	# UPDATE: Grass Types & Alpha
	# SVEN Logic:
	# - Grass Type (Texture) = Layer1 Index (0, 1, 2)
	# - Grass ONLY renders if Alpha Blending is NOT active (Alpha == 0)
	
	res.grass_tiles = {} 
	
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		var type = res.layer1[i]
		
		# SVEN supports grass on Indices 0, 1, 2 (TileGrass01, 02, 03)
		if type <= 2:
			# Strict Alpha Check: If blended (> 0.5), suppress grass (Roads/Transitions)
			# Road is usually Layer 2. If Alpha is high, Layer 2 is dominant.
			# We want grass only where Layer 1 is dominant (Alpha low).
			if res.alpha[i] <= 0.5:
				if not res.grass_tiles.has(type):
					res.grass_tiles[type] = []
					
				var x = i % TERRAIN_SIZE
				var y = i / TERRAIN_SIZE
				res.grass_tiles[type].append(Vector2i(x, y))
	
	var total_grass = 0
	for t in res.grass_tiles:
		total_grass += res.grass_tiles[t].size()
		print("[Terrain Parser] Grass Type %d: %d tiles" % [t, res.grass_tiles[t].size()])
		
	if total_grass > 0:
		print("[Terrain Parser] Total Grass Tiles: %d" % total_grass)
		
	return res

func parse_attributes_file(path: String) -> PackedByteArray:
	# Parse terrain attributes (collision/walkability) from .att file
	var file = MUFileUtil.open_file(path, FileAccess.READ)
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
	var file = MUFileUtil.open_file(path, FileAccess.READ)
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
