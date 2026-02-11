@tool
extends SceneTree

const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")
const MUCoordUtils = preload("res://addons/mu_tools/core/mu_coordinate_utils.gd")

func _init():
	print("\n======================================================================")
	print("MU COORDINATE VIZ TEST")
	print("======================================================================")
	
	# Hypothetical Rectangle in MU Space
	# Center: (10000, 10000) (Units)
	# Size: 200 units wide (X), 100 units deep (Y)
	var center = Vector3(10000, 10000, 0)
	var corners_mu = [
		center + Vector3(100, 50, 0),  # Top-Right
		center + Vector3(-100, 50, 0), # Top-Left
		center + Vector3(-100, -50, 0),# Bottom-Left
		center + Vector3(100, -50, 0)  # Bottom-Right
	]
	
	print("\n1. POSITION MAPPING (X_mu, Y_mu) -> (X_go, Z_go)")
	print("--------------------------------------------------")
	for i in range(corners_mu.size()):
		var m = corners_mu[i]
		var g = MUTransformPipeline.world_mu_to_godot(m)
		var g2 = MUCoordUtils.mu_to_godot(m.x / 100.0, m.y / 100.0, m.z)
		print("Corner %d: MU(%5.0f, %5.0f) -> Pipeline(%6.1f, %6.1f) | Utils(%6.1f, %6.1f)" % [
			i, m.x, m.y, g.x, g.z, g2.x, g2.z
		])

	print("\n2. ORIENTATION SYNC (Angle.z = CCW)")
	print("--------------------------------------------------")
	var angles = [0, 90, 180, 270]
	for a in angles:
		var q = MUTransformPipeline.mu_rotation_to_quaternion(Vector3(0, 0, deg_to_rad(a)))
		var forward = q * Vector3(0, 0, -1)
		var right = q * Vector3(1, 0, 0)
		print("MU Angle %3d: Fwd=%s | Right=%s" % [a, str(forward), str(right)])

	print("\n3. PINWHEEL ANALYSIS")
	print("--------------------------------------------------")
	print("If we map (X, Y) -> (Y, -X) [Pipeline Mapping]:")
	print("  MU North (+X) -> Godot North (-Z)")
	print("  MU East  (+Y) -> Godot East  (+X)")
	print("If an object at corner (10, 5) faces angle 0 (North):")
	print("  In Godot it should be at (5, 256-10=246) facing -Z.")
	print("\nIf we map (X, Y) -> (X, -Y) [Utils Mapping]:")
	print("  MU North (+X) -> Godot East  (+X)")
	print("  MU East  (+Y) -> Godot North (-Z)")
	
	print("\nINCONSISTENCY DETECTED:")
	print("- Pipeline: G_X = Y_mu, G_Z = 256 - X_mu")
	print("- Utils:    G_X = X_mu, G_Z = 256 - Y_mu")
	
	quit()
