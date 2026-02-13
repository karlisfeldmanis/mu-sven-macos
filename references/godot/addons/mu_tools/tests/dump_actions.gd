@tool
extends SceneTree

func _init():
	var parser_script = load("res://addons/mu_tools/parsers/bmd_parser.gd")
	var parser = parser_script.new()
	var path = "res://reference/MuMain/src/bin/Data/Object1/Waterspout01.bmd"
	var err = parser.parse_file(path)
	if err != OK:
		print("Failed to parse: ", path)
		quit()
		return
	
	print("Model: ", path)
	print("Actions: ", parser.actions.size())
	for i in range(parser.actions.size()):
		var action = parser.actions[i]
		print("Action %d: %d keys" % [i, action.keys.size()])
		for b in range(min(5, action.keys.size())):
			var keys = action.keys[b]
			if keys and keys.size() > 0:
				print("  Bone %d translation: %s" % [b, str(keys[0].position)])
	quit()
