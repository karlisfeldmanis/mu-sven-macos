@tool
extends SceneTree

## Import all BMD models from reference data into assets/models/ as OBJ.
## Usage:
##   godot --path . --headless -s scripts/import_models.gd
##
## Optional args (via -- separator):
##   --source <path>   Source directory to scan (default: reference data)
##   --output <path>   Output directory (default: res://assets/models)
##   --filter <name>   Only import BMDs matching this substring

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUOBJExporter = preload(
	"res://addons/mu_tools/core/mu_obj_exporter.gd"
)

var source_dir := "res://reference/MuMain/src/bin/Data"
var output_dir := "res://assets/models"
var filter := ""


func _init():
	_parse_args()
	_run_import()
	quit()


func _parse_args():
	var args = OS.get_cmdline_user_args()
	var i = 0
	while i < args.size():
		match args[i]:
			"--source":
				i += 1
				if i < args.size():
					source_dir = args[i]
			"--output":
				i += 1
				if i < args.size():
					output_dir = args[i]
			"--filter":
				i += 1
				if i < args.size():
					filter = args[i].to_lower()
		i += 1


func _run_import():
	print("=" .repeat(50))
	print("[Import] BMD → OBJ Import Script")
	print("[Import] Source: %s" % source_dir)
	print("[Import] Output: %s" % output_dir)
	if filter != "":
		print("[Import] Filter: %s" % filter)
	print("=" .repeat(50))

	# Collect all BMD files
	var bmd_files: Array[String] = []
	_scan_bmd_files(source_dir, bmd_files)
	bmd_files.sort()

	if bmd_files.is_empty():
		print("[Import] No BMD files found in: %s" % source_dir)
		return

	print("[Import] Found %d BMD files" % bmd_files.size())

	var parser = BMDParser.new()
	var success = 0
	var skipped = 0
	var failed = 0

	for bmd_path in bmd_files:
		var base = bmd_path.get_file().get_basename()

		# Apply filter
		if filter != "" and not base.to_lower().contains(filter):
			continue

		# Check if already exported
		var obj_path = output_dir.path_join(base + ".obj")
		if FileAccess.file_exists(obj_path):
			skipped += 1
			continue

		# Ensure output dir
		if not DirAccess.dir_exists_absolute(output_dir):
			DirAccess.make_dir_recursive_absolute(output_dir)

		# Parse
		if not parser.parse_file(bmd_path, false):
			print("[Import] ✗ Parse failed: %s" % base)
			failed += 1
			continue

		# Export
		if MUOBJExporter.export_bmd(parser, bmd_path, output_dir):
			print("[Import] ✓ %s" % base)
			success += 1
		else:
			print("[Import] ✗ Export failed: %s" % base)
			failed += 1

	print("-" .repeat(50))
	print(
		"[Import] Done. Imported: %d | Skipped: %d | Failed: %d"
		% [success, skipped, failed]
	)


func _scan_bmd_files(dir_path: String, out: Array):
	var dir = DirAccess.open(dir_path)
	if not dir:
		return
	dir.list_dir_begin()
	var name = dir.get_next()
	while name != "":
		var full = dir_path.path_join(name)
		if dir.current_is_dir() and not name.begins_with("."):
			_scan_bmd_files(full, out)
		elif name.to_lower().ends_with(".bmd"):
			out.append(full)
		name = dir.get_next()
