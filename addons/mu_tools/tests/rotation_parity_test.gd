extends SceneTree

# Rotation Parity Test Script
# Verifies MU Euler -> Godot Quaternion conversion logic

const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")

func _init():
	print("======================================================================")
	print("MU ROTATION PARITY TEST")
	print("======================================================================")
	
	test_rotation_conversion()
	
	print("======================================================================")
	quit()

func test_rotation_conversion():
	var cases = [
		{
			"name": "Identity",
			"mu_euler": Vector3(0, 0, 0),
			"expected_forward": Vector3(0, 0, -1)
		},
		{
			"name": "Yaw 90 (MU Z)",
			"mu_euler": Vector3(0, 0, deg_to_rad(90)),
			"expected_forward": Vector3(1, 0, 0) # Face Right (+X)
		},
		{
			"name": "Pitch 90 (MU Y)",
			"mu_euler": Vector3(0, deg_to_rad(90), 0),
			"expected_forward": Vector3(0, -1, 0) # Face Down (-Y)
		},
		{
			"name": "Roll 90 (MU X)",
			"mu_euler": Vector3(deg_to_rad(90), 0, 0),
			"expected_forward": Vector3(0, 0, -1),
			"expected_up": Vector3(-1, 0, 0) # Roll Left
		}
	]
	
	print("\n[ORIGINAL LOGIC: (-Pitch, -Yaw, Roll) YXZ]")
	run_test_batch(cases, func(e): return Basis.from_euler(Vector3(-e.y, -e.z, e.x), EULER_ORDER_YXZ).get_rotation_quaternion())
	
	print("\n[CANDIDATE FIX 1: (Pitch, -Yaw, -Roll) YXZ]")
	run_test_batch(cases, func(e): return Basis.from_euler(Vector3(e.y, -e.z, -e.x), EULER_ORDER_YXZ).get_rotation_quaternion())

	print("\n[CANDIDATE FIX 2: (-Pitch, Yaw, Roll) YXZ]")
	run_test_batch(cases, func(e): return Basis.from_euler(Vector3(-e.y, e.z, e.x), EULER_ORDER_YXZ).get_rotation_quaternion())

func run_test_batch(cases, conversion_func):
	var pass_count = 0
	for c in cases:
		var q = conversion_func.call(c.mu_euler)
		var basis = Basis(q)
		var forward = basis * Vector3(0, 0, -1)
		var up = basis * Vector3(0, 1, 0)
		
		var f_match = forward.is_equal_approx(c.expected_forward)
		var u_match = true
		if c.has("expected_up"):
			u_match = up.is_equal_approx(c.expected_up)
			
		if f_match and u_match:
			pass_count += 1
		else:
			print("❌ FAIL: %s (Fwd=%s, Up=%s)" % [c.name, forward, up])
				
	print("Result: %d/%d passed" % [pass_count, cases.size()])
