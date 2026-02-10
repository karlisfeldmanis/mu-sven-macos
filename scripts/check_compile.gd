extends SceneTree

func _init():
	print("Checking MURenderAPI...")
	var script = load("res://addons/mu_tools/core/mu_render_api.gd")
	if not script:
		print("FAILED to load script resource")
	else:
		var inst = script.new()
		if not inst:
			print("FAILED to instantiate MURenderAPI")
		else:
			print("SUCCESSfully instantiated MURenderAPI")
	quit()
