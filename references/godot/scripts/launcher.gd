extends Node

## MU Online Remaster - Bootstrap Launcher
## Performs mandatory health check before launching the windowed client.

const PreflightValidator = preload("res://addons/mu_tools/util/preflight_validator.gd")

func _ready():
	print("\n[BOOTSTRAP] Performing pre-launch health check...")
	
	var errors = PreflightValidator.run_full_check()
	
	if errors > 0:
		printerr("\n[BOOTSTRAP] FATAL: Found ", errors, " errors. Launch aborted.")
		get_tree().quit(1)
		return

	print("\n[BOOTSTRAP] Health check passed. Launching client...")
	get_tree().call_deferred("change_scene_to_file", "res://scenes/main.tscn")
