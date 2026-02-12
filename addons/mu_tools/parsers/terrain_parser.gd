extends Node

# class_name MUTerrainParser
const MUFileUtil = preload("res://addons/mu_tools/core/file_util.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")

# XOR key for map and object files decryption
const MAP_XOR_KEY = [
	0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
	0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
]

const TERRAIN_SIZE = 256

# Attribute Bitmasks (Sven Parity)
const TW_SAFEZONE = 0x0001
const TW_CHARACTER = 0x0002
const TW_NOMOVE = 0x0004
const TW_NOGROUND = 0x0008
const TW_WATER = 0x0010
const TW_ACTION = 0x0020
const TW_HEIGHT = 0x0040
const TW_CAMERA_UP = 0x0080
const TW_NOATTACKZONE = 0x0100

class MapData:
	var map_number: int
	var layer1: PackedByteArray # 256x256
	var layer2: PackedByteArray # 256x256
	var alpha: PackedFloat32Array # 256x256
	var water_tiles: Array # Positions of water tiles (Vector2i) - index 5
	var grass_tiles: Dictionary # { type: [Vector2i, ...], ... }
	var attributes: PackedByteArray # 256x256 Attribute Map
	
	func is_walkable(gx: int, gz: int) -> bool:
		var idx = gz * TERRAIN_SIZE + gx
		if idx < 0 or idx >= attributes.size(): return false
		var att = attributes[idx]
		# NOMOVE (0x04) or NOGROUND (0x08) means it's NOT walkable
		return (att & (TW_NOMOVE | TW_NOGROUND)) == 0
		
	func is_safe_zone(gx: int, gz: int) -> bool:
		var idx = gz * TERRAIN_SIZE + gx
		if idx < 0 or idx >= attributes.size(): return false
		return (attributes[idx] & TW_SAFEZONE) != 0

	func is_water(gx: int, gz: int) -> bool:
		var idx = gz * TERRAIN_SIZE + gx
		if idx < 0 or idx >= attributes.size(): return false
		return (attributes[idx] & TW_WATER) != 0

class ObjectData:
	var type: int
	var position: Vector3
	var rotation: Quaternion
	var scale: float
	var mu_euler: Vector3 # Raw decidegrees
	var mu_pos_raw: Vector3 # Raw MU world units
	var hidden_mesh: int = -1 # -2 = hidden (SVEN: HiddenMesh), -1 = visible

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
	
	# 1084 bytes for OZB (1080 header + 4 potential prefix)
	# 66620 - 65536 = 1084
	var expected_size = TERRAIN_SIZE * TERRAIN_SIZE
	var offset = file.get_length() - expected_size
	file.seek(offset)
		
	var raw_heights = file.get_buffer(expected_size)
	
	var heights = PackedFloat32Array()
	heights.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	
	# heights is PackedFloat32Array[256*256]
	# Row-Major: GodotZ * 256 + GodotX
	# SVEN Y-Flip: GodotZ = (255 - mu_y)
	for mu_y in range(TERRAIN_SIZE):
		for mu_x in range(TERRAIN_SIZE):
			var idx = mu_y * TERRAIN_SIZE + mu_x
			# heights[idx] = float(raw_heights[idx]) # Previous raw storage
			# Standard MU scaling: 1.5 units per byte
			# SVEN Parity: Apply -500.0 MU unit offset (g_fMinHeight = -500.f)
			# (1.5 * raw - 500.0) / 100.0 = (1.5 * raw / 100.0) - 5.0
			heights[idx] = (float(raw_heights[idx]) * 1.5) / 100.0
			
	return heights

func parse_mapping_file(path: String, encrypted: bool = true) -> MapData:
	var file = MUFileUtil.open_file(path, FileAccess.READ)
	if not file:
		return null
		
	var raw_data = file.get_buffer(file.get_length())
	var data = raw_data
	
	if encrypted:
		data = decrypt_map_file(raw_data)
	
	print("[Terrain Parser] Mapping Data Size: %d (Expected >= %d)" % [
		data.size(), 2 + TERRAIN_SIZE * TERRAIN_SIZE * 3
	])
	
	# Dynamic Header Skip (Sven Parity)
	# Encrypted .map (Dungeon) is usually 196610 (2 byte header)
	# Raw .map (Lorencia) is usually 196609 (1 byte header)
	var expected_layers_size = TERRAIN_SIZE * TERRAIN_SIZE * 3
	var ptr = max(0, data.size() - expected_layers_size)
	
	print("[Terrain Parser] %s: Data size %d. Skipping %d bytes header." % [path.get_file(), data.size(), ptr])
	
	var res = MapData.new()
	res.layer1 = data.slice(ptr, ptr + TERRAIN_SIZE * TERRAIN_SIZE)
	ptr += TERRAIN_SIZE * TERRAIN_SIZE
	
	var raw_layer2 = data.slice(ptr, ptr + TERRAIN_SIZE * TERRAIN_SIZE)
	res.layer2 = raw_layer2 # Store for debugging
	res.alpha.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	ptr += TERRAIN_SIZE * TERRAIN_SIZE
	
	var raw_alpha_start = ptr
	
	# Standard South-to-North Godot Row-Major Fill
	for mu_y in range(TERRAIN_SIZE):
		for mu_x in range(TERRAIN_SIZE):
			var idx = mu_y * TERRAIN_SIZE + mu_x
			if raw_alpha_start + idx < data.size():
				var val = float(data[raw_alpha_start + idx]) / 255.0
				res.alpha[idx] = val
			else:
				res.alpha[idx] = 0.0

	ptr += TERRAIN_SIZE * TERRAIN_SIZE # Advance past alpha block
	

	# Post-Process: Apply Shoreline Overrides (Sven Parity)
	# MOVED: Now called explicitly via apply_attributes() after attributes are loaded.

	# Standard Godot Indexing (GodotZ * 256 + GodotX)
	# Linear Scan of flipped data
	for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
		if res.layer1[i] == 5:
			var gx = i % TERRAIN_SIZE
			var gz = i / TERRAIN_SIZE
			if res.alpha[i] < 0.5:
				res.water_tiles.append(Vector2i(gx, gz))
	
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
		
		# SVEN supports grass on Indices 0, 1 (TileGrass01, 02)
		# Index 2 is usually TileGround01 (Dirt), so we exclude it.
		if type <= 1:
			# SVEN Logic: Relaxed threshold to show more grass near blended edges
			if res.alpha[i] < 0.4:
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
	
func apply_attributes(res: MapData, attributes: PackedByteArray):
	if attributes.size() != TERRAIN_SIZE * TERRAIN_SIZE:
		push_error("Attribute size mismatch for apply_attributes")
		return
		
	res.attributes = attributes

func parse_attributes_file(path: String) -> Dictionary:
	# Parse terrain attributes (collision/walkability) from .att file
	# SVEN Parity: Format can be 65536 (BYTE) or 131076 (WORD)
	var file = MUFileUtil.open_file(path, FileAccess.READ)
	if not file:
		push_error("Cannot open attributes file: ", path)
		return {"collision": PackedByteArray(), "symmetry": PackedByteArray()}
	
	var encrypted_data = file.get_buffer(file.get_length())
	print("[Terrain Parser] ATT Raw Size: %d Path: %s" % [encrypted_data.size(), path])
	var data = decrypt_map_file(encrypted_data)
	print("[Terrain Parser] ATT Decrypted Size: %d" % data.size())
	
	# Apply BuxConvert (Additional XOR for .att files)
	var bux_code = [0xFC, 0xCF, 0xAB]
	for i in range(data.size()):
		data[i] ^= bux_code[i % 3]
	
	var collision = PackedByteArray()
	collision.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	var symmetry = PackedByteArray()
	symmetry.resize(TERRAIN_SIZE * TERRAIN_SIZE)
	
	var expected_word_size = 4 + (TERRAIN_SIZE * TERRAIN_SIZE * 2)
	
	if data.size() == expected_word_size:
		# WORD format (2 bytes per tile)
		# Byte 0: Collision (TW_... flags)
		# Byte 1: Symmetry (Rotation/Mirroring)
		for i in range(TERRAIN_SIZE * TERRAIN_SIZE):
			collision[i] = data[4 + i * 2]
			symmetry[i] = data[5 + i * 2]
	elif data.size() >= TERRAIN_SIZE * TERRAIN_SIZE:
		# Fallback: BYTE format (Lorencia/World 1)
		# Size matches 1 byte per tile + 1 header (65537) or similar.
		var offset = data.size() - TERRAIN_SIZE * TERRAIN_SIZE
		collision = data.slice(offset)
		symmetry.fill(0) # Definitive: World 1 has no tile symmetry
		
	print("[Terrain Parser] Loaded %d attribute bytes (Parity: WORD=%s)" % [collision.size(), data.size() == expected_word_size])
	return {"collision": collision, "symmetry": symmetry}

func parse_objects_file(path: String, world_id: int = -1) -> Array[ObjectData]:
	var file = MUFileUtil.open_file(path, FileAccess.READ)
	if not file:
		return []

	var encrypted_data = file.get_buffer(file.get_length())
	var data = decrypt_map_file(encrypted_data)

	var objects: Array[ObjectData] = []
	var ptr = 0

	# OpenObjectsEnc header (SVEN ZzzObject.cpp:4944-4948):
	# Byte 0: Version (BYTE)
	# Byte 1: MapNumber (BYTE)
	# Bytes 2-3: Count (short)
	var version = data[0]
	var map_number = data[1]
	ptr = 2

	if world_id >= 0 and map_number != (world_id + 1):
		push_warning(
			"[Terrain Parser] Object file map number %d != expected %d"
			% [map_number, world_id + 1])

	var count = data.decode_s16(ptr)
	ptr += 2

	print("[Terrain Parser] Loading %d objects from %s (v%d, map %d)"
		% [count, path.get_file(), version, map_number])
	
	for i in range(count):
		var obj = ObjectData.new()
		obj.type = data.decode_s16(ptr)
		ptr += 2
		
		# Read MU world-space position (vec3_t)
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
		
		# Convert using MUCoordinateUtils (Standard Mapping)
		var mu_rad = Vector3(deg_to_rad(mu_angle.x), deg_to_rad(mu_angle.y), deg_to_rad(mu_angle.z))
		obj.position = MUCoordinateUtils.convert_object_position(mu_pos)
		obj.rotation = MUCoordinateUtils.convert_object_rotation(mu_rad)
		obj.mu_euler = mu_angle
		obj.mu_pos_raw = mu_pos
		
		# SVEN compatibility: Set HiddenMesh for invisible objects
		# MODEL_POSE_BOX = 133 (interactive pose trigger, invisible)
		if obj.type == 133: # MODEL_POSE_BOX
			obj.hidden_mesh = -2
		
		# Diagnostic logging for placement issues
		if i < 3000:
			print("  [OBJ] %d T=%d MU=(%.0f,%.0f,%.0f) G=(%.1f,%.1f,%.1f) R=(%.0f,%.0f,%.0f) S=%.2f"
				% [i, obj.type,
				mu_pos.x, mu_pos.y, mu_pos.z,
				obj.position.x, obj.position.y, obj.position.z,
				mu_angle.x, mu_angle.y, mu_angle.z, obj.scale])
		
		objects.append(obj)
		
	return objects
