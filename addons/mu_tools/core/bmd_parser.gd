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
	var vertex_colors: Array[Color] = []          # Version 13+
	var texture_filename: String = ""
	var raw_data: PackedByteArray                  # Optional: Full mesh binary footprint

class BMDTriangle:
	var polygon_type: int = 3                      # 3=tri, 4=quad (from file)
	var vertex_indices: PackedInt32Array
	var normal_indices: PackedInt32Array
	var uv_indices: PackedInt32Array
	var lightmap_uvs: Array[Vector2] = []          # 4 UV pairs from Triangle_t2
	var lightmap_index: int = -1                   # LightMapIndexes from Triangle_t2
	var edge_triangle_indices: PackedInt32Array    # From Triangle_t (unverified alignment)
	var front_face: bool = true                     # From Triangle_t
	var raw_data: PackedByteArray                  # Original 64-byte block for "Maximum Data"

class BMDBone:
	var name: String
	var parent_index: int = -1
	var is_dummy: bool = false
	var position: Vector3
	var rotation: Vector3
	var scale: Vector3
	var raw_header: PackedByteArray               # 35-byte binary header for inspection

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

	var sig = raw_data.slice(0, 3).get_string_from_ascii()
	if sig != "BMD":
		push_error("[BMD Parser] Invalid signature: " + sig)
		return false

	var version = raw_data[3]
	var decrypted_data: PackedByteArray
	var stream = StreamPeerBuffer.new()
	stream.big_endian = false

	if version == 0x0C: # Version 12 — encrypted
		stream.data_array = raw_data.slice(4, 8)
		var _enc_size = stream.get_u32()
		decrypted_data = MUDecryptor.decrypt_bmd(raw_data, version, 8)
	elif version == 0x0A: # Version 10 — raw
		decrypted_data = raw_data.slice(4)
	else:
		push_error("[BMD Parser] Unsupported version: 0x%02X" % version)
		return false

	stream.data_array = decrypted_data
	stream.seek(0)

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
		print("[BMD Parser] Version=0x%02X Name='%s' Meshes=%d Bones=%d Actions=%d" % [
			header.version, header.model_name, header.mesh_count, header.bone_count, header.action_count])

	if not _parse_meshes(stream, debug): return false
	if not _parse_actions(stream, debug): return false
	if not _parse_bones(stream, debug): return false

	return true

func _parse_meshes(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for mesh_idx in range(header.mesh_count):
		var mesh = BMDMesh.new()
		mesh.vertex_count = stream.get_u16()
		mesh.normal_count = stream.get_u16()
		mesh.uv_count = stream.get_u16()
		mesh.triangle_count = stream.get_u16()
		mesh.texture_index = stream.get_u16()

		for i in range(mesh.vertex_count):
			mesh.vertex_nodes.append(stream.get_16())
			stream.get_16() # alignment padding
			mesh.vertices.append(Vector3(stream.get_float(), stream.get_float(), stream.get_float()))

		for i in range(mesh.normal_count):
			mesh.normal_nodes.append(stream.get_16())
			stream.get_16() # alignment padding
			mesh.normals.append(Vector3(stream.get_float(), stream.get_float(), stream.get_float()))
			mesh.normal_bind_vertices.append(stream.get_16())
			stream.get_16() # alignment padding

		for i in range(mesh.uv_count):
			mesh.uv_coords.append(Vector2(stream.get_float(), stream.get_float()))

		if header.version >= 0x0D:
			var num_vc = stream.get_u16()
			for i in range(num_vc):
				mesh.vertex_colors.append(Color(stream.get_8()/255.0, stream.get_8()/255.0, stream.get_8()/255.0, 1.0))

		for i in range(mesh.triangle_count):
			var tri_pos = stream.get_position()
			var tri = BMDTriangle.new()
			tri.raw_data = stream.data_array.slice(tri_pos, tri_pos + 64)
			
			tri.polygon_type = stream.get_8()
			stream.get_8() # padding
			
			var vi = [stream.get_16(), stream.get_16(), stream.get_16(), stream.get_16()]
			var ni = [stream.get_16(), stream.get_16(), stream.get_16(), stream.get_16()]
			var uvi = [stream.get_16(), stream.get_16(), stream.get_16(), stream.get_16()]
			
			# Triangles 12 bytes into header, total structure 64
			stream.get_16() # padding before lightmap
			for k in range(4):
				tri.lightmap_uvs.append(Vector2(stream.get_float(), stream.get_float()))
			tri.lightmap_index = stream.get_16()
			stream.get_16() # tail padding
			
			tri.vertex_indices = PackedInt32Array([vi[0], vi[1], vi[2]])
			tri.normal_indices = PackedInt32Array([ni[0], ni[1], ni[2]])
			tri.uv_indices = PackedInt32Array([uvi[0], uvi[1], uvi[2]])
			mesh.triangles.append(tri)
			
			if tri.polygon_type == 4:
				var tri2 = BMDTriangle.new()
				tri2.polygon_type = 4
				tri2.vertex_indices = PackedInt32Array([vi[0], vi[2], vi[3]])
				tri2.normal_indices = PackedInt32Array([ni[0], ni[2], ni[3]])
				tri2.uv_indices = PackedInt32Array([uvi[0], uvi[2], uvi[3]])
				tri2.lightmap_uvs = tri.lightmap_uvs
				tri2.lightmap_index = tri.lightmap_index
				tri2.raw_data = tri.raw_data
				mesh.triangles.append(tri2)

		var tex_data = stream.get_data(32)[1] as PackedByteArray
		var tex_null = tex_data.find(0)
		mesh.texture_filename = tex_data.slice(0, tex_null).get_string_from_ascii().strip_edges() if tex_null != -1 else tex_data.get_string_from_ascii().strip_edges()
		
		meshes.append(mesh)
	return true

func _parse_actions(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for action_idx in range(header.action_count):
		var action = BMDAction.new()
		action.frame_count = stream.get_u16()
		action.lock_positions = stream.get_8() != 0
		if action.lock_positions and action.frame_count > 0:
			for f in range(action.frame_count):
				action.lock_position_data.append(Vector3(stream.get_float(), stream.get_float(), stream.get_float()))
		action.bone_count = header.bone_count
		action.keys = []
		action.keys.resize(header.bone_count)
		actions.append(action)
	return true

func _parse_bones(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for bone_idx in range(header.bone_count):
		var bone = BMDBone.new()
		var bone_start = stream.get_position()
		var is_dummy = stream.get_8() != 0
		bone.is_dummy = is_dummy

		if not is_dummy:
			bone.raw_header = stream.data_array.slice(bone_start, bone_start + 35)
			var name_bytes = stream.get_data(32)[1] as PackedByteArray
			bone.name = name_bytes.get_string_from_ascii().strip_edges()
			bone.parent_index = stream.get_16()

			for action_idx in range(header.action_count):
				var action = actions[action_idx]
				var num_keys = action.frame_count
				var bone_keys = []
				if num_keys > 0:
					for f in range(num_keys):
						bone_keys.append({"position": Vector3(stream.get_float(), stream.get_float(), stream.get_float())})
					for f in range(num_keys):
						bone_keys[f].rotation = Vector3(stream.get_float(), stream.get_float(), stream.get_float())
						bone_keys[f].converted_quat = MUCoordinateUtils.bmd_angle_to_quaternion(bone_keys[f].rotation)
				action.keys[bone_idx] = bone_keys
		else:
			bone.name = "Dummy_%d" % bone_idx
			bone.parent_index = -1
		bones.append(bone)
	return true
