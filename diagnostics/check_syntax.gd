extends SceneTree

func _init():
	var scripts = [
		"res://addons/mu_tools/nodes/mu_terrain.gd",
		"res://addons/mu_tools/nodes/mu_environment.gd",
		"res://scenes/lorencia_effects/falling_leaves.gd"
	]
	for s in scripts:
		var script = load(s)
		if not script:
			print("FAILED: ", s)
		else:
			print("OK: ", s)
	quit()
