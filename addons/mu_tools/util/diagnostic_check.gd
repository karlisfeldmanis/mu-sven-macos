extends SceneTree

func _init():
	var errs = PreflightValidator.run_full_check()
	if errs > 0:
		print("[DIAGNOSTIC] Found %d script errors." % errs)
	else:
		print("[DIAGNOSTIC] All scripts validated successfully.")
	quit(errs)
