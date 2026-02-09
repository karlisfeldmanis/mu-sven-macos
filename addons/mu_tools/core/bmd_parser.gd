@tool
class_name BMDParser
const MUFileUtil = preload("res://addons/mu_tools/core/mu_file_util.gd")
const MUDecryptor = preload("res://addons/mu_tools/core/mu_decryptor.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/core/mu_coordinate_utils.gd")

## BMD File Parser (Phase 2-4)
##
## Parses MuOnline .bmd files containing:
## - Meshes (vertices, normals, UVs, skinning data)
## - Skeleton (bone hierarchy)
## - Animations (keyframe actions)
##
## Based on ZzzBMD.cpp from Sven-n/MuMain
## Reference: https://github.com/sven-n/MuMain/blob/main/src/ZzzBMD.cpp

## BMD file structure
class BMDHeader:
	var signature: String
	var version: int
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
	var vertex_nodes: PackedInt32Array = []
	var normal_nodes: PackedInt32Array = []
	var normals: Array[Vector3] = []
	var uv_coords: Array[Vector2] = []
	var triangles: Array[BMDTriangle] = []
	var texture_filename: String = ""
	var render_flags: int = 0 # Mesh rendering properties (transparency, alpha)
	var flags: int # Placeholder for extra data

class BMDVertex:
	var position: Vector3
	var normal: Vector3
	var uv: Vector2
	var bone_indices: PackedByteArray  # 4 bytes
	var bone_weights: PackedByteArray  # 4 bytes (0-255 range)

class BMDTriangle:
	var vertex_indices: PackedInt32Array
	var normal_indices: PackedInt32Array
	var uv_indices: PackedInt32Array

class BMDBone:
	var name: String
	var parent_index: int  # -1 for root bones
	var position: Vector3
	var rotation: Vector3  # Euler angles in degrees
	var scale: Vector3

class BMDAction:
	var frame_count: int
	var bone_count: int
	var fps: float
	var lock_positions: bool
	var keys: Array  # Array of keyframe data per bone

var header: BMDHeader
var meshes: Array[BMDMesh] = []
var bones: Array[BMDBone] = []
var actions: Array[BMDAction] = []

## Parse a BMD file from path
func parse_file(path: String, debug: bool = false) -> bool:
	var file = MUFileUtil.open_file(path, FileAccess.READ)
	if not file:
		push_error("[BMD Parser] Failed to open file: " + path)
		return false
	
	file.big_endian = false
	var raw_data = file.get_buffer(file.get_length())
	file.close()

	if raw_data.size() < 4:
		return false

	# 1. Read signature and version
	var sig = raw_data.slice(0, 3).get_string_from_ascii()
	if sig != "BMD":
		push_error("[BMD Parser] Invalid signature: " + sig)
		return false
	
	var version = raw_data[3]
	var decrypted_data: PackedByteArray
	var stream = StreamPeerBuffer.new()
	stream.big_endian = false
	
	if version == 0x0C: # Version 12 (Encrypted)
		stream.data_array = raw_data.slice(4, 8)
		var enc_size = stream.get_u32()
		decrypted_data = MUDecryptor.decrypt_bmd(raw_data, version, 8)
	elif version == 0x0A: # Version 10 (Raw)
		decrypted_data = raw_data.slice(4)
	else:
		push_error("[BMD Parser] Unsupported version: %d" % version)
		return false
	
	stream.data_array = decrypted_data
	stream.seek(0)
	
	if debug:
		var slice = decrypted_data.slice(0, 64)
		print("[BMD Parser] Decrypted Header Hex: ", slice.hex_encode())
	
	# Skip name (32 bytes - confirm via hex if meshes still 0)
	stream.seek(32)
	
	# Read header counts
	header = BMDHeader.new()
	header.signature = sig
	header.version = version
	header.mesh_count = stream.get_u16()
	header.bone_count = stream.get_u16()
	header.action_count = stream.get_u16()
	
	if debug:
		var mc = header.mesh_count
		var bc = header.bone_count
		var ac = header.action_count
		print("[BMD Parser] Header: Meshes=%d, Bones=%d, Actions=%d" % [mc, bc, ac])

	
	# 1. Parse meshes (Phase 2)
	if not _parse_meshes(stream, debug):
		return false
	
	# 2. Parse action metadata (Needed for bone frame sizes)
	if not _parse_actions(stream, debug):
		return false
		
	# 3. Parse bones and their animation keys (Phase 3 & 4 interleaved)
	if not _parse_bones(stream, debug):
		return false
	
	return true

# Removed _parse_header as it's merged into parse_file

## Parse mesh data (Phase 2)
func _parse_meshes(stream: StreamPeerBuffer, _debug: bool) -> bool:
	for mesh_idx in range(header.mesh_count):
		if _debug: print("  [BMD Parser] Parsing mesh %d/%d" % [mesh_idx + 1, header.mesh_count])
		var mesh = BMDMesh.new()
		
		# Mesh Header (10 bytes for v10, 12 bytes for v12)
		# NOTE: Some v10 files (Beer01) have 4 bytes of 00 padding between meshes.
		# We check for V=0, N=0 as a signature of padding (since valid meshes with V=0 like effects usually have N>0).
		
		mesh.vertex_count = stream.get_u16()
		mesh.normal_count = stream.get_u16()
			
		mesh.uv_count = stream.get_u16()
		mesh.triangle_count = stream.get_u16()
		mesh.texture_index = stream.get_u16()
		
		if _debug:
			print("    [BMD Parser] Mesh %d: V=%d N=%d UV=%d T=%d TexIdx=%d" % [
				mesh_idx, mesh.vertex_count, mesh.normal_count, mesh.uv_count, 
				mesh.triangle_count, mesh.texture_index
			])
		
		# Effect Mesh Safeguard (e.g., Mesh 4 in Beer01)
		# MU Online effect meshes can have T > 0 but V = 0, and use a different struct size.
		# To avoid global drift, we skip them if they don't fit the standard 64-byte Triangle_t2 pattern.
		if mesh.vertex_count == 0 and mesh.triangle_count > 0:
			if _debug: print("    [BMD Parser] Warning: Effect mesh detected (V=0), skipping body to prevent drift.")
			# Proactively find next texture string to verify safe recovery
			var current_pos = stream.get_position()
			var next_tex_pos = -1
			for j in range(current_pos, stream.data_array.size() - 40):
				var s = stream.data_array.slice(j, j+10).get_string_from_ascii()
				if s.contains(".jpg") or s.contains(".tga") or s.contains(".OZJ") or s.contains(".OZT") or s.contains(".ozj") or s.contains(".ozt"):
					next_tex_pos = j
					break
			if next_tex_pos != -1:
				stream.seek(next_tex_pos)
			else:
				return false # Critical error
		else:
			# 1. Vertices (16 bytes: short node + 2b pad + vec3 pos)
			for i in range(mesh.vertex_count):
				mesh.vertex_nodes.append(stream.get_16())
				stream.get_16() # Alignment padding
				var vpos = Vector3(stream.get_float(), stream.get_float(), stream.get_float())
				mesh.vertices.append(vpos)
				
			if _debug and mesh.vertices.size() > 0:
				var v0 = mesh.vertices[0]
				var n0 = mesh.vertex_nodes[0]
				print("[BMD Parser] Mesh %d First Vertex: %s (Node: %d)" % [mesh_idx, v0, n0])
				
			# 2. Normals (20 bytes: short node + 2b pad + vec3 norm + short bind + 2b pad)
			for i in range(mesh.normal_count):
				mesh.normal_nodes.append(stream.get_16())
				stream.get_16() # pad
				var nvec = Vector3(stream.get_float(), stream.get_float(), stream.get_float())
				mesh.normals.append(nvec)
				var _bind = stream.get_16()
				stream.get_16() # pad
				
			# 3. UVs (8 bytes: float u + float v)
			for i in range(mesh.uv_count):
				mesh.uv_coords.append(Vector2(stream.get_float(), stream.get_float()))
				
			# 4. Triangles (Triangle_t2 struct, 64-byte aligned in file)
			for i in range(mesh.triangle_count):
				var tri_offset = stream.get_position()
				
				# Triangle header (Polygon index + alignment)
				var poly_type = stream.get_8()
				stream.get_8() # Padding (1 byte)
				
				# vi[4], ni[4], uvi[4] (all short)
				var vi = []; for k in range(4): vi.append(stream.get_16())
				var ni = []; for k in range(4): ni.append(stream.get_16())
				var uvi = []; for k in range(4): uvi.append(stream.get_16())
				
				# Triangle 1 (Indices 0, 1, 2)
				var tri1 = BMDTriangle.new()
				tri1.vertex_indices = PackedInt32Array([vi[0], vi[1], vi[2]]) # Store raw
				tri1.normal_indices = PackedInt32Array([ni[0], ni[1], ni[2]])
				tri1.uv_indices = PackedInt32Array([uvi[0], uvi[1], uvi[2]])
				mesh.triangles.append(tri1)
				
				# Triangle 2 (Indices 0, 2, 3) if Quad
				if poly_type == 4:
					var tri2 = BMDTriangle.new()
					tri2.vertex_indices = PackedInt32Array([vi[0], vi[2], vi[3]])
					tri2.normal_indices = PackedInt32Array([ni[0], ni[2], ni[3]])
					tri2.uv_indices = PackedInt32Array([uvi[0], uvi[2], uvi[3]])
					mesh.triangles.append(tri2)
				
				# Jump to next triangle (Total 64 bytes)
				stream.seek(tri_offset + 64)
				
			# 5. Texture Filename (32 bytes, null-terminated)
			var tex_data = stream.get_data(32)[1] as PackedByteArray
			var null_pos = tex_data.find(0)
			var raw_name = ""
			if null_pos != -1:
				raw_name = tex_data.slice(0, null_pos).get_string_from_ascii()
			else:
				raw_name = tex_data.get_string_from_ascii()
			
			mesh.texture_filename = raw_name.strip_edges()
			if _debug: print("  [BMD Parser] Mesh %d texture filename: '%s'" % [mesh_idx, mesh.texture_filename])
		
		meshes.append(mesh)
	return true

## Parse bone hierarchy (Phase 3)
func _parse_bones(stream: StreamPeerBuffer, _debug: bool) -> bool:
	# Note: In Open2 version 10/12, bone data is read AFTER action metadata
	# and interleaved with animation frames.
	for bone_idx in range(header.bone_count):
		var bone = BMDBone.new()
		var is_dummy = stream.get_8() != 0
		
		if not is_dummy:
			var name_bytes = stream.get_data(32)
			bone.name = (name_bytes[1] as PackedByteArray).get_string_from_ascii().strip_edges()
			bone.parent_index = stream.get_16()
			
			# Read animation keys for this bone for ALL actions
			for action_idx in range(header.action_count):
				if actions.is_empty(): # Prevent out-of-bounds access if actions array is unexpectedly empty
					continue
				var action = actions[action_idx]
				var num_keys = action.frame_count
				var bone_keys = []
				
				if num_keys > 0:
					for f in range(num_keys):
						var key = {}
						key.position = Vector3(
							stream.get_float(),
							stream.get_float(),
							stream.get_float()
						)
						bone_keys.append(key)
					
					# 2. Read all rotations sequentially (MU native format)
					for f in range(num_keys):
						bone_keys[f].rotation = Vector3(
							stream.get_float(),
							stream.get_float(),
							stream.get_float()
						)
						var converted_quat = MUCoordinateUtils.bmd_angle_to_quaternion(
							bone_keys[f].rotation
						)
						bone_keys[f].converted_quat = converted_quat
				
				action.keys[bone_idx] = bone_keys
				if _debug and bone_idx == 0 and action_idx == 0 \
						and not bone_keys.is_empty():
					var k0 = bone_keys[0]
					print("[BMD Parser] Bone %d Action %d Rot: %s" \
							% [bone_idx, action_idx, k0.rotation])
		else:
			bone.name = "Dummy_%d" % bone_idx
			bone.parent_index = -1
			
		if _debug and bone_idx < 10:
			print("[BMD Parser] Bone %d: parent=%d name=%s" % 
					[bone_idx, bone.parent_index, bone.name])
			
		bones.append(bone)
	return true

## Parse animation actions (Phase 4)
func _parse_actions(stream: StreamPeerBuffer, _debug: bool) -> bool:
	# In Open2, this loop only reads action metadata.
	# The actual keys are interleaved in the bone loop!
	for action_idx in range(header.action_count):
		var action = BMDAction.new()
		action.frame_count = stream.get_u16()
		action.lock_positions = stream.get_8() != 0 # LockPositions field in file
		
		if action.lock_positions and action.frame_count > 0:
			# Skip translation adjustment data (vec3 * frame_count)
			var jump = 12 * action.frame_count
			var new_pos = stream.get_position() + jump
			if new_pos <= stream.data_array.size():
				stream.seek(new_pos)
			else:
				if _debug: print("    [BMD Parser] Warning: Action jump overflow")
				stream.seek(stream.data_array.size())
				break # Stop parsing actions if corrupt
			
		action.bone_count = header.bone_count
		action.keys = []
		action.keys.resize(header.bone_count)
		action.fps = 25.0
		
		actions.append(action)
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
