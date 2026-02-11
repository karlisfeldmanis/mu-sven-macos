@tool
extends SceneTree

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")

func _init():
	print("\n======================================================================")
	print("MINIMAP BMD INSPECTOR")
	print("======================================================================")
	
	var world_id = 1
	var bmd_path = "res://reference/MuMain/src/bin/Data/World%d/Minimap.bmd" % world_id
	
	var parser = BMDParser.new()
	if not parser.parse_file(bmd_path, true):
		print("FAILED to parse Minimap.bmd")
		quit()
		return
		
	print("Bones: %d" % parser.bones.size())
	print("Meshes: %d" % parser.meshes.size())
	print("Actions: %d" % parser.actions.size())
	
	for i in range(parser.bones.size()):
		var bone = parser.bones[i]
		print("Bone %d: name=%s, pos=%s" % [i, bone.name, bone.position])
		
	quit()
