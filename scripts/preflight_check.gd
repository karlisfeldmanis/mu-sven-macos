extends SceneTree

## MU Online Remaster - Preflight Check (Headless Launcher)
## Legacy wrapper for the robust PreflightValidator class.

func _init():
	var Validator = load("res://addons/mu_tools/util/preflight_validator.gd")
	var errors = Validator.run_full_check()
	
	print("\n" + "=".repeat(60))
	if errors > 0:
		printerr("[PREFLIGHT] FAILED: Found ", errors, " validation errors.")
		print("=".repeat(60) + "\n")
		quit(1)
	else:
		print("[PREFLIGHT] SUCCESS: All scripts and resources passed validation.")
		print("=".repeat(60) + "\n")
		quit(0)
