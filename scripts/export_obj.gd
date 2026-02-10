@tool
extends SceneTree

# Headless BMD to OBJ Converter CLI

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUOBJExporter = preload("res://addons/mu_tools/core/mu_obj_exporter.gd")

func _init():
	var args = OS.get_cmdline_user_args()
	var input_path = ""
	var output_dir = ""
	var debug = false
	
	for i in range(args.size()):
		if args[i] == "--input" and i + 1 < args.size():
			input_path = args[i + 1]
		elif args[i] == "--output" and i + 1 < args.size():
			output_dir = args[i + 1]
		elif args[i] == "--debug":
			debug = true

	if input_path == "" or output_dir == "":
		print("Usage: godot --headless -s scripts/export_obj.gd -- --input <file.bmd> --output <res://path/> [--debug]")
		quit()
		return

	_export(input_path, output_dir, debug)
	quit()

func _export(input_path: String, output_dir: String, debug: bool):
	print("[CLI] Exporting OBJ: ", input_path)
	
	if not FileAccess.file_exists(input_path):
		push_error("[CLI] Input files does not exist: " + input_path)
		return
		
	var parser = BMDParser.new()
	if not parser.parse_file(input_path, debug):
		push_error("[CLI] Failed to parse BMD: " + input_path)
		return

	if not DirAccess.dir_exists_absolute(output_dir):
		DirAccess.make_dir_recursive_absolute(output_dir)

	if MUOBJExporter.export_bmd(parser, input_path, output_dir):
		print("[CLI] Successfully exported to: ", output_dir)
	else:
		push_error("[CLI] Failed to export OBJ.")
