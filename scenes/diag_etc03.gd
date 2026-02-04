@tool
extends SceneTree

func _init():
	var files = [
		"res://reference/MuMain/src/bin/Data/Object1/DoungeonGate01.bmd",
		"res://reference/MuMain/src/bin/Data/Object1/HouseEtc02.bmd"
	]
	
	for path in files:
		var parser = BMDParser.new()
		if not parser.parse_file(path):
			print("FAILED to parse: ", path)
			continue
			
		print("--- Bone Hierarchy for: ", path, " ---")
		for i in range(parser.bones.size()):
			var bone = parser.bones[i]
			print("Bone %d: name='%s' parent=%d local_pos=%v" % [
				i, bone.name, bone.parent_index, bone.position
			])
			
		print("Mesh count: ", parser.meshes.size())
		for i in range(parser.meshes.size()):
			var mesh = parser.meshes[i]
			var node_counts = {}
			for n in mesh.vertex_nodes:
				node_counts[n] = node_counts.get(n, 0) + 1
			print("  Mesh %d: total_vertices=%d texture='%s'" % [
				i, mesh.vertex_count, mesh.texture_filename
			])
			for n in node_counts:
				print("    Node %d: %d vertices" % [n, node_counts[n]])
		print("")
		
	quit()
