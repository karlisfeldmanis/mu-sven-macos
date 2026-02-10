@tool
extends SceneTree

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")

func _init():
	var args = OS.get_cmdline_user_args()
	if args.is_empty(): quit(); return
	
	var path = args[0]
	var parser = BMDParser.new()
	if not parser.parse_file(path, false):
		print("Failed to parse: ", path)
		quit(); return
		
	print("BMD: ", path.get_file())
	print("Bones: ", parser.bones.size())
	for i in range(parser.bones.size()):
		var b = parser.bones[i]
		print("Bone[%d]: parent=%d pos=%s rot=%s" % [i, b.parent_index, b.position, b.rotation])
		
	print("\nMeshes: ", parser.meshes.size())
	for m_idx in range(parser.meshes.size()):
		var mesh = parser.meshes[m_idx]
		var nodes = mesh.vertex_nodes
		var unique_nodes = {}
		for n in nodes: unique_nodes[n] = true
		var node_keys = unique_nodes.keys()
		node_keys.sort()
		
		var first_v = mesh.vertices[0] if mesh.vertices.size() > 0 else Vector3.ZERO
		print("  Mesh[%d]: tex=%s bone_indices=%s v_sample[0]=%s" % [m_idx, mesh.texture_filename, node_keys, first_v])
		
	if not parser.actions.is_empty():
		print("\nAction 0 Keys:")
		var act0 = parser.actions[0]
		for i in range(min(act0.keys.size(), parser.bones.size())):
			var keys = act0.keys[i]
			if keys != null and not keys.is_empty():
				print("  Key[%d]: pos=%s rot=%s" % [i, keys[0].position, keys[0].rotation])
	
	quit()
