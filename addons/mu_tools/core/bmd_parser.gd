@tool
class_name BMDParser
const MUFileUtil = preload("res://addons/mu_tools/core/mu_file_util.gd")
const MUDecryptor = preload("res://addons/mu_tools/core/mu_decryptor.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/core/mu_coordinate_utils.gd")

## BMD File Parser — Faithful to ZzzBMD.cpp Open2
##
## Parses MuOnline .bmd files containing:
## - Meshes (vertices, normals, UVs, triangles, textures)
## - Skeleton (bone hierarchy)
## - Animations (keyframe actions)
##
## Binary layout (C++ struct sizes, MSVC alignment, version-independent):
##   Vertex_t    = 16 bytes (short Node + 2pad + vec3 Position)
##   Normal_t    = 20 bytes (short Node + 2pad + vec3 Normal + short BindVertex + 2pad)
##   TexCoord_t  =  8 bytes (float U + float V)
##   Triangle_t2 = 64 bytes (see _parse_meshes for full layout)
##   Texture     = 32 bytes (char FileName[32])
##
## Reference: https://github.com/sven-n/MuMain/blob/main/src/ZzzBMD.cpp

# ── Data Classes ────────────────────────────────────────────────────────────

class BMDHeader:
	var signature: String
	var version: int
	var model_name: String = ""
	var mesh_count: int
	var bone_count: int
	var action_count: int

class BMDMesh:
	var vertex_count: int
	var normal_count: int
	var uv_count: int
	var triangle_count: int
	var texture_index: int
	var vertices: Array[Vector3] = []
	var vertex_nodes: PackedInt32Array = []       # Per-vertex bone index
	var normals: Array[Vector3] = []
	var normal_nodes: PackedInt32Array = []        # Per-normal bone index
	var normal_bind_vertices: PackedInt32Array = [] # Per-normal: index of bound vertex
	var uv_coords: Array[Vector2] = []
	var triangles: Array[BMDTriangle] = []
	var texture_filename: String = ""

class BMDTriangle:
	var polygon_type: int = 3                      # 3=tri, 4=quad (from file)
	var vertex_indices: PackedInt32Array
	var normal_indices: PackedInt32Array
	var uv_indices: PackedInt32Array
	var lightmap_uvs: Array[Vector2] = []          # 4 UV pairs from Triangle_t2
	var lightmap_index: int = -1                   # LightMapIndexes from Triangle_t2

class BMDBone:
	var name: String
	var parent_index: int = -1
	var position: Vector3
	var rotation: Vector3
	var scale: Vector3

class BMDAction:
	var frame_count: int
	var bone_count: int
	var fps: float = 25.0
	var lock_positions: bool
	var lock_position_data: Array[Vector3] = []    # Per-frame root motion offsets
	var keys: Array                                # Per-bone keyframe arrays

# ── State ───────────────────────────────────────────────────────────────────

var header: BMDHeader
var meshes: Array[BMDMesh] = []
var bones: Array[BMDBone] = []
var actions: Array[BMDAction] = []

# ── Public API ──────────────────────────────────────────────────────────────

## Parse a BMD file from path
func parse_file(path: String, debug: bool = false) -> bool:
	meshes.clear()
	bones.clear()
	actions.clear()
	header = null

	var file = MUFileUtil.open_file(path, FileAccess.READ)
	if not file:
		push_error("[BMD Parser] Failed to open file: " + path)
		return false

	file.big_endian = false
	var raw_data = file.get_buffer(file.get_length())
	file.close()

	if raw_data.size() < 4:
		return false

	# ── Signature & Version ──
	var sig = raw_data.slice(0, 3).get_string_from_ascii()
	if sig != "BMD":
		push_error("[BMD Parser] Invalid signature: " + sig)
		return false

	var version = raw_data[3]
	var decrypted_data: PackedByteArray
	var stream = StreamPeerBuffer.new()
	stream.big_endian = false

	if version == 0x0C: # Version 12 — encrypted (MapFileDecrypt)
		stream.data_array = raw_data.slice(4, 8)
		var _enc_size = stream.get_u32()
		decrypted_data = MUDecryptor.decrypt_bmd(raw_data, version, 8)
	elif version == 0x0A: # Version 10 — raw (no encryption)
		decrypted_data = raw_data.slice(4)
	else:
		push_error("[BMD Parser] Unsupported version: 0x%02X" % version)
		return false

	stream.data_array = decrypted_data
	stream.seek(0)

	if debug:
		print("[BMD Parser] Decrypted Header Hex: ", decrypted_data.slice(0, 64).hex_encode())

	# ── Header (offset 0) ──
	# Model name: 32 bytes, null-terminated ASCII (e.g. "ArmorClass01.smd")
	var name_data = stream.get_data(32)[1] as PackedByteArray
	var null_pos = name_data.find(0)
	var model_name_str = ""
	if null_pos != -1:
		model_name_str = name_data.slice(0, null_pos).get_string_from_ascii()
	else:
		model_name_str = name_data.get_string_from_ascii()

	header = BMDHeader.new()
	header.signature = sig
	header.version = version
	header.model_name = model_name_str.strip_edges()
	header.mesh_count = stream.get_u16()
	header.bone_count = stream.get_u16()
	header.action_count = stream.get_u16()

	if debug:
		print("[BMD Parser] Name='%s' Meshes=%d Bones=%d Actions=%d" % [
			header.model_name, header.mesh_count, header.bone_count, header.action_count])

	# ── Parse in C++ Open2 order: Meshes → Actions → Bones ──
	if not _parse_meshes(stream, debug):
		return false
	if not _parse_actions(stream, debug):
		return false
	if not _parse_bones(stream, debug):
		return false

	return true

## Get mesh count
func get_mesh_count() -> int:
	return meshes.size()

## Get bone count
func get_bone_count() -> int:
	return bones.size()

## Get specific mesh
func get_mesh(index: int) -> BMDMesh:
	if index >= 0 and index < meshes.size():
		return meshes[index]
	return null

## Get specific bone
func get_bone(index: int) -> BMDBone:
	if index >= 0 and index < bones.size():
		return bones[index]
	return null

# ── Mesh Parsing ────────────────────────────────────────────────────────────
# C++ Open2 reads meshes linearly: header(10) + V*Vertex_t(16) + N*Normal_t(20)
# + UV*TexCoord_t(8) + T*Triangle_t2(64) + Texture(32).
# No special cases for effect meshes (V=0). Loops simply execute 0 times.

func _parse_meshes(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for mesh_idx in range(header.mesh_count):
		if _debug:
			print("  [BMD Parser] Parsing mesh %d/%d (offset 0x%04X)" % [
				mesh_idx + 1, header.mesh_count, stream.get_position()])
		var mesh = BMDMesh.new()

		# Mesh Header — 10 bytes (5 x u16)
		mesh.vertex_count = stream.get_u16()
		mesh.normal_count = stream.get_u16()
		mesh.uv_count = stream.get_u16()
		mesh.triangle_count = stream.get_u16()
		mesh.texture_index = stream.get_u16()

		if _debug:
			print("    V=%d N=%d UV=%d T=%d TexIdx=%d" % [
				mesh.vertex_count, mesh.normal_count, mesh.uv_count,
				mesh.triangle_count, mesh.texture_index])

		# Vertex_t — 16 bytes each: i16 Node + i16 pad + f32 x,y,z
		for i in range(mesh.vertex_count):
			mesh.vertex_nodes.append(stream.get_16())
			stream.get_16() # alignment padding
			mesh.vertices.append(Vector3(
				stream.get_float(), stream.get_float(), stream.get_float()))

		if _debug and mesh.vertices.size() > 0:
			print("    First vertex: %s (node %d)" % [mesh.vertices[0], mesh.vertex_nodes[0]])

		# Normal_t — 20 bytes each: i16 Node + i16 pad + f32 x,y,z + i16 BindVertex + i16 pad
		for i in range(mesh.normal_count):
			mesh.normal_nodes.append(stream.get_16())
			stream.get_16() # alignment padding
			mesh.normals.append(Vector3(
				stream.get_float(), stream.get_float(), stream.get_float()))
			mesh.normal_bind_vertices.append(stream.get_16())
			stream.get_16() # alignment padding

		# TexCoord_t — 8 bytes each: f32 U + f32 V
		for i in range(mesh.uv_count):
			mesh.uv_coords.append(Vector2(stream.get_float(), stream.get_float()))

		# Triangle_t2 — 64 bytes each (full layout):
		#   Offset  0: char  Polygon          (1B)
		#   Offset  1: pad                    (1B)
		#   Offset  2: short VertexIndex[4]   (8B)
		#   Offset 10: short NormalIndex[4]   (8B)
		#   Offset 18: short TexCoordIndex[4] (8B)
		#   Offset 26: pad                    (2B)
		#   Offset 28: TexCoord_t LightMapCoord[4] (32B)
		#   Offset 60: short LightMapIndexes  (2B)
		#   Offset 62: pad                    (2B)
		for i in range(mesh.triangle_count):
			var tri_start = stream.get_position()

			var poly_type = stream.get_8()  # 3=tri, 4=quad
			stream.get_8()                  # padding

			var vi = []; for k in range(4): vi.append(stream.get_16())
			var ni = []; for k in range(4): ni.append(stream.get_16())
			var uvi = []; for k in range(4): uvi.append(stream.get_16())

			stream.get_16() # padding before lightmap

			var lm_uvs: Array[Vector2] = []
			for k in range(4):
				lm_uvs.append(Vector2(stream.get_float(), stream.get_float()))
			var lm_idx = stream.get_16()

			stream.get_16() # struct tail padding

			# Triangle 1 (indices 0, 1, 2)
			var tri1 = BMDTriangle.new()
			tri1.polygon_type = poly_type
			tri1.vertex_indices = PackedInt32Array([vi[0], vi[1], vi[2]])
			tri1.normal_indices = PackedInt32Array([ni[0], ni[1], ni[2]])
			tri1.uv_indices = PackedInt32Array([uvi[0], uvi[1], uvi[2]])
			tri1.lightmap_uvs = lm_uvs
			tri1.lightmap_index = lm_idx
			mesh.triangles.append(tri1)

			# Triangle 2 (indices 0, 2, 3) if quad
			if poly_type == 4:
				var tri2 = BMDTriangle.new()
				tri2.polygon_type = poly_type
				tri2.vertex_indices = PackedInt32Array([vi[0], vi[2], vi[3]])
				tri2.normal_indices = PackedInt32Array([ni[0], ni[2], ni[3]])
				tri2.uv_indices = PackedInt32Array([uvi[0], uvi[2], uvi[3]])
				tri2.lightmap_uvs = lm_uvs
				tri2.lightmap_index = lm_idx
				mesh.triangles.append(tri2)

			if _debug:
				var actual_pos = stream.get_position()
				var expected_pos = tri_start + 64
				if actual_pos != expected_pos:
					push_error("[BMD Parser] Triangle struct size mismatch at mesh %d tri %d: expected 0x%04X, got 0x%04X" % [
						mesh_idx, i, expected_pos, actual_pos])

		# Texture filename — 32 bytes, null-terminated ASCII
		var tex_data = stream.get_data(32)[1] as PackedByteArray
		var tex_null = tex_data.find(0)
		if tex_null != -1:
			mesh.texture_filename = tex_data.slice(0, tex_null).get_string_from_ascii().strip_edges()
		else:
			mesh.texture_filename = tex_data.get_string_from_ascii().strip_edges()

		if _debug:
			print("    Texture: '%s'" % mesh.texture_filename)

		meshes.append(mesh)
	return true

# ── Action Metadata ─────────────────────────────────────────────────────────
# C++ Open2: reads NumAnimationKeys(u16) + LockPositions(u8) per action.
# If LockPositions, reads vec3_t[NumAnimationKeys] root motion offsets.

func _parse_actions(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for action_idx in range(header.action_count):
		var action = BMDAction.new()
		action.frame_count = stream.get_u16()
		action.lock_positions = stream.get_8() != 0

		if action.lock_positions and action.frame_count > 0:
			var bytes_needed = 12 * action.frame_count
			if stream.get_position() + bytes_needed > stream.data_array.size():
				push_error("[BMD Parser] Action %d LockPositions overflows buffer" % action_idx)
				return false
			for f in range(action.frame_count):
				action.lock_position_data.append(Vector3(
					stream.get_float(), stream.get_float(), stream.get_float()))

		action.bone_count = header.bone_count
		action.keys = []
		action.keys.resize(header.bone_count)
		action.fps = 25.0

		if _debug:
			print("  [BMD Parser] Action %d: frames=%d lock=%s" % [
				action_idx, action.frame_count, action.lock_positions])

		actions.append(action)
	return true

# ── Bone & Keyframe Parsing ─────────────────────────────────────────────────
# C++ Open2: for each bone, reads Dummy(u8). If not dummy: Name(32) + Parent(i16)
# then for each action: Position[frames] + Rotation[frames] (all vec3_t = 12 bytes).

func _parse_bones(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for bone_idx in range(header.bone_count):
		var bone = BMDBone.new()
		var is_dummy = stream.get_8() != 0

		if not is_dummy:
			var name_bytes = stream.get_data(32)
			bone.name = (name_bytes[1] as PackedByteArray).get_string_from_ascii().strip_edges()
			bone.parent_index = stream.get_16()

			for action_idx in range(header.action_count):
				if actions.is_empty():
					continue
				var action = actions[action_idx]
				var num_keys = action.frame_count
				var bone_keys = []

				if num_keys > 0:
					# Positions: vec3_t[num_keys]
					for f in range(num_keys):
						var key = {}
						key.position = Vector3(
							stream.get_float(), stream.get_float(), stream.get_float())
						bone_keys.append(key)

					# Rotations: vec3_t[num_keys]
					for f in range(num_keys):
						bone_keys[f].rotation = Vector3(
							stream.get_float(), stream.get_float(), stream.get_float())
						bone_keys[f].converted_quat = MUCoordinateUtils.bmd_angle_to_quaternion(
							bone_keys[f].rotation)

				if bone_idx < action.keys.size():
					action.keys[bone_idx] = bone_keys
				else:
					action.keys.resize(bone_idx + 1)
					action.keys[bone_idx] = bone_keys

				if _debug and bone_idx == 0 and action_idx == 0 and not bone_keys.is_empty():
					print("    Bone 0 Action 0 Frame 0: pos=%s rot=%s" % [
						bone_keys[0].position, bone_keys[0].rotation])
		else:
			bone.name = "Dummy_%d" % bone_idx
			bone.parent_index = -1

		if _debug and bone_idx < 5:
			print("  [BMD Parser] Bone %d: parent=%d name='%s' dummy=%s" % [
				bone_idx, bone.parent_index, bone.name, is_dummy])

		bones.append(bone)
	return true
