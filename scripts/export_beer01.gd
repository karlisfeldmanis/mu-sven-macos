@tool
extends SceneTree

# Focused Export for Beer01 (Type 151)
# Clears extracted_data and exports only this asset.

const MUModelRegistry = preload("res://addons/mu_tools/core/mu_model_registry.gd")
const MUOBJExporter = preload("res://addons/mu_tools/core/mu_obj_exporter.gd")
const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")

func _init():
	_export_beer()
	quit()

func _export_beer():
	var output_dir = "res://extracted_data/object_models"
	print("[BeerExport] Starting Focused Export to: ", output_dir)
	
	# 1. Clean Directory
	if DirAccess.dir_exists_absolute(output_dir):
		print("[BeerExport] Cleaning old files...")
		var dir = DirAccess.open(output_dir)
		dir.list_dir_begin()
		var file_name = dir.get_next()
		while file_name != "":
			if not dir.current_is_dir():
				dir.remove(file_name)
			file_name = dir.get_next()
	else:
		DirAccess.make_dir_recursive_absolute(output_dir)
		
	# 2. Identify Path
	# Type 151 is Beer01 in World 1
	var path = MUModelRegistry.get_object_path(151, 1)
	if path == "":
		print("[BeerExport] ❌ Could not resolve path for Type 151 (Beer01)")
		return
		
	print("[BeerExport] Resolved Path: ", path)
	
	if not FileAccess.file_exists(path):
		print("[BeerExport] ❌ File not found: ", path)
		return

	# 3. Export
	var parser = BMDParser.new()
	if not parser.parse_file(path, false): # false = no verbose debug inside parser unless error
		print("[BeerExport] ❌ Parse Failed")
		return
		
	if MUOBJExporter.export_bmd(parser, path, output_dir):
		print("[BeerExport] ✓ Successfully Exported Beer01")
		
		# Validate Output
		var obj_path = output_dir.path_join("Beer01.obj")
		var mtl_path = output_dir.path_join("Beer01.mtl")
		var tex_path = output_dir.path_join("Beer01.png") # Expected texture name?
		
		if FileAccess.file_exists(obj_path):
			print("  - OBJ Exists")
		else:
			print("  - ❌ OBJ Missing")
			
		if FileAccess.file_exists(mtl_path):
			print("  - MTL Exists")
			
		# Texture name might vary, check directory listing
		print("  - Generated Files:")
		var dir = DirAccess.open(output_dir)
		dir.list_dir_begin()
		var f = dir.get_next()
		while f != "":
			if not dir.current_is_dir():
				print("    * ", f)
			f = dir.get_next()
			
	else:
		print("[BeerExport] ❌ Export Failed")
