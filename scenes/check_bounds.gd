@tool
extends SceneTree

const BMDParser = preload("res://addons/mu_tools/bmd_parser.gd")

func _init():
	check_bmd("res://reference/MuMain/src/bin/Data/Object1/Fence01.bmd")
	check_bmd("res://reference/MuMain/src/bin/Data/Player/ArmorClass01.bmd")
	quit()

func check_bmd(path: String):
	print("Checking: ", path)
	var parser = BMDParser.new()
	if not parser.parse_file(ProjectSettings.globalize_path(path)):
		print("Failed to parse")
		return

	var min_v = Vector3(99999, 99999, 99999)
	var max_v = Vector3(-99999, -99999, -99999)
	
	for mesh in parser.meshes:
		for v in mesh.vertices:
			min_v.x = min(min_v.x, v.x)
			min_v.y = min(min_v.y, v.y)
			min_v.z = min(min_v.z, v.z)
			max_v.x = max(max_v.x, v.x)
			max_v.y = max(max_v.y, v.y)
			max_v.z = max(max_v.z, v.z)
	
	print("  RAW MU-Space Bounding Box:")
	print("    Min: ", min_v)
	print("    Max: ", max_v)
	print("    Size: ", max_v - min_v)
