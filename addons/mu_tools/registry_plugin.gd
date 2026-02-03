@tool
extends EditorPlugin

# MU Registry Plugin (Phase 0)
# Registers custom extensions and handles common MU setup.

var texture_importer
var bmd_importer

func _enter_tree():
	# Register .ozj and .ozt as texture files
	texture_importer = load("res://addons/mu_tools/texture_importer.gd").new()
	add_import_plugin(texture_importer)
	
	# Register .bmd as mesh/scene files
	# Note: BMD importer will be added in Phase 2
	# bmd_importer = load("res://addons/mu_tools/bmd_importer.gd").new()
	# add_import_plugin(bmd_importer)
	
	print("[MU Registry] Registered MU Online extensions (.ozj, .ozt, .bmd)")

func _exit_tree():
	remove_import_plugin(texture_importer)
	if bmd_importer:
		remove_import_plugin(bmd_importer)
