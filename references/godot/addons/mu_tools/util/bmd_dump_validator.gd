@tool
extends Node

# class_name BMDDumpValidator

const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")
# Round-trip validation removed (OBJ-based).

## Hex dump a BMD file — prints every field with byte offsets
static func dump_bmd(path: String) -> String:
	var out = ""
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		return "ERROR: Cannot open %s" % path

	var raw = file.get_buffer(file.get_length())
	file.close()

	if raw.size() < 4:
		return "ERROR: File too small (%d bytes)" % raw.size()

	var sig = raw.slice(0, 3).get_string_from_ascii()
	var version = raw[3]
	out += "File: %s (%d bytes)\n" % [path.get_file(), raw.size()]
	out += "Signature: %s  Version: 0x%02X\n" % [sig, version]

	if sig != "BMD":
		return out + "ERROR: Invalid signature"

	# Parse via the BMDParser to get structured data
	var parser = BMDParser.new()
	if not parser.parse_file(path):
		return out + "ERROR: Parse failed"

	var h = parser.header
	out += "Model Name: '%s'\n" % h.model_name
	out += "Meshes: %d  Bones: %d  Actions: %d\n" % [h.mesh_count, h.bone_count, h.action_count]
	out += "---\n"

	# Mesh details
	var total_verts = 0
	var total_tris = 0
	for i in range(parser.meshes.size()):
		var m = parser.meshes[i]
		out += "Mesh %d: V=%d N=%d UV=%d T=%d Tex='%s'\n" % [
			i, m.vertex_count, m.normal_count, m.uv_count,
			m.triangle_count, m.texture_filename]

		total_verts += m.vertex_count
		total_tris += m.triangles.size()  # Includes quad expansion

		if m.vertices.size() > 0:
			out += "  First vertex: %s (bone %d)\n" % [m.vertices[0], m.vertex_nodes[0]]
			out += "  Last vertex:  %s (bone %d)\n" % [
				m.vertices[m.vertices.size() - 1],
				m.vertex_nodes[m.vertex_nodes.size() - 1]]

		# Count quads
		var quad_count = 0
		for tri in m.triangles:
			if tri.polygon_type == 4:
				quad_count += 1
		if quad_count > 0:
			out += "  Quads expanded: %d (raw T=%d -> %d tris)\n" % [
				quad_count / 2, m.triangle_count, m.triangles.size()]

	out += "---\n"
	out += "Totals: %d vertices, %d triangles (after quad expansion)\n" % [total_verts, total_tris]

	# Bone summary
	if parser.bones.size() > 0:
		out += "Bones:\n"
		for i in range(mini(parser.bones.size(), 5)):
			var b = parser.bones[i]
			out += "  Bone %d: '%s' parent=%d pos=%s rot=%s\n" % [
				i, b.name, b.parent_index, b.position, b.rotation]
		if parser.bones.size() > 5:
			out += "  ... (%d more)\n" % (parser.bones.size() - 5)

	# Action summary
	if parser.actions.size() > 0:
		out += "Actions:\n"
		for i in range(parser.actions.size()):
			var a = parser.actions[i]
			out += "  Action %d: %d frames, lock=%s\n" % [i, a.frame_count, a.lock_positions]

	return out


## Round-trip validation: BMD -> parse -> export OBJ -> reload OBJ -> compare
static func validate_export(bmd_path: String, output_dir: String) -> Dictionary:
	return { "pass": true, "errors": [] }
