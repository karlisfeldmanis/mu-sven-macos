@tool
extends SceneTree

## Systematic validation of the MU toolchain.
## Parses all Lorencia Object1 BMDs, exports to OBJ, validates round-trip.

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const BMDDumpValidator = preload("res://addons/mu_tools/util/bmd_dump_validator.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")

const OBJECT1_DIR = "res://reference/MuMain/src/bin/Data/Object1"
const OUTPUT_DIR = "res://assets/models/Object1"

func _init():
	print("=".repeat(60))
	print("[Validation] Lorencia Object1 Pipeline Audit")
	print("=".repeat(60))

	_test_coordinate_mapping()
	print("")
	_batch_validate_lorencia()

	print("\n[Validation] Complete.")
	quit()

func _test_coordinate_mapping():
	print("[Test] Coordinate Mapping (MU -> Godot)")
	var mu_v = Vector3(100, 200, 300) # MU X, Y, Z
	var godot_v = MUTransformPipeline.local_mu_to_godot(mu_v)

	# local_mu_to_godot: (-X, Z, -Y) * 0.01
	var expected = Vector3(-1.0, 3.0, -2.0)
	if godot_v.is_equal_approx(expected):
		print("  PASS  Local Mapping: (-X, Z, -Y) * 0.01")
	else:
		print("  FAIL  Local Mapping: Got %s, Expected %s" % [godot_v, expected])

func _batch_validate_lorencia():
	print("[Test] Batch BMD -> OBJ Validation (Object1)")

	var dir = DirAccess.open(OBJECT1_DIR)
	if not dir:
		print("  FAIL  Cannot open %s" % OBJECT1_DIR)
		return

	# Collect all BMD files
	var bmd_files: Array[String] = []
	dir.list_dir_begin()
	var fname = dir.get_next()
	while fname != "":
		if fname.get_extension().to_lower() == "bmd":
			bmd_files.append(fname)
		fname = dir.get_next()
	dir.list_dir_end()
	bmd_files.sort()

	print("  Found %d BMD files" % bmd_files.size())
	print("-".repeat(60))

	var pass_count = 0
	var fail_count = 0
	var parse_fail_count = 0
	var failures: Array[String] = []

	for bmd_name in bmd_files:
		var bmd_path = OBJECT1_DIR.path_join(bmd_name)
		var result = BMDDumpValidator.validate_export(bmd_path, OUTPUT_DIR)

		if result.pass:
			pass_count += 1
			print("  PASS  %-30s V=%d T=%d" % [bmd_name, result.bmd_verts, result.bmd_tris])
		elif not result.parse_ok:
			parse_fail_count += 1
			print("  FAIL  %-30s PARSE ERROR" % bmd_name)
			failures.append("%s: %s" % [bmd_name, ", ".join(result.errors)])
		else:
			fail_count += 1
			var errs = ", ".join(result.errors)
			print("  FAIL  %-30s %s" % [bmd_name, errs])
			failures.append("%s: %s" % [bmd_name, errs])

	print("-".repeat(60))
	print("[Summary] %d/%d passed, %d failed, %d parse errors" % [
		pass_count, bmd_files.size(), fail_count, parse_fail_count])

	if not failures.is_empty():
		print("\n[Failures]")
		for f in failures:
			print("  - %s" % f)
