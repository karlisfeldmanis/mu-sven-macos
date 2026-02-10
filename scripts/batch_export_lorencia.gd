@tool
extends SceneTree

## Batch export all Lorencia Object1 BMDs to OBJ+MTL+PNG

const MUOBJExporter = preload("res://addons/mu_tools/core/mu_obj_exporter.gd")
const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")

const OBJECT1_DIR = "res://reference/MuMain/src/bin/Data/Object1"
const OUTPUT_DIR = "res://assets/models/Object1"

func _init():
	_batch_export()
	quit()

func _batch_export():
	print("=".repeat(60))
	print("[Batch Export] Lorencia Object1 -> OBJ")
	print("  Source: %s" % OBJECT1_DIR)
	print("  Output: %s" % OUTPUT_DIR)
	print("=".repeat(60))

	DirAccess.make_dir_recursive_absolute(OUTPUT_DIR)

	var dir = DirAccess.open(OBJECT1_DIR)
	if not dir:
		print("ERROR: Cannot open %s" % OBJECT1_DIR)
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

	print("Found %d BMD files\n" % bmd_files.size())

	var parser = BMDParser.new()
	var success_count = 0
	var fail_parse = 0
	var fail_export = 0

	for bmd_name in bmd_files:
		var bmd_path = OBJECT1_DIR.path_join(bmd_name)

		if not parser.parse_file(bmd_path):
			print("  FAIL  %-30s parse error" % bmd_name)
			fail_parse += 1
			continue

		if MUOBJExporter.export_bmd(parser, bmd_path, OUTPUT_DIR):
			var mesh_count = 0
			var vert_count = 0
			for m in parser.meshes:
				if m.vertex_count > 0:
					mesh_count += 1
					vert_count += m.vertex_count
			print("  OK    %-30s meshes=%d verts=%d" % [bmd_name, mesh_count, vert_count])
			success_count += 1
		else:
			print("  FAIL  %-30s export error" % bmd_name)
			fail_export += 1

	print("\n" + "-".repeat(60))
	print("[Done] %d exported, %d parse errors, %d export errors (of %d total)" % [
		success_count, fail_parse, fail_export, bmd_files.size()])
