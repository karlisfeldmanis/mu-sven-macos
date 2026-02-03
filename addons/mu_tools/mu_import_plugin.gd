@tool
extends EditorPlugin

## MU Tools - Main Plugin Entry Point
##
## This plugin provides import capabilities for MuOnline proprietary formats:
## - .ozj/.ozt (textures)
## - .bmd (models with meshes, skeletons, and animations)

var texture_importer: EditorImportPlugin
var bmd_importer: EditorImportPlugin

func _enter_tree() -> void:
	# Phase 1: Texture importer
	texture_importer = preload("res://addons/mu_tools/texture_importer.gd").new()
	add_import_plugin(texture_importer)
	
	# Phase 2-4: BMD importer (will be implemented later)
	# bmd_importer = preload("res://addons/mu_tools/bmd_importer.gd").new()
	# add_import_plugin(bmd_importer)
	
	print("[MU Tools] Plugin loaded successfully")

func _exit_tree() -> void:
	if texture_importer:
		remove_import_plugin(texture_importer)
		texture_importer = null
	
	if bmd_importer:
		remove_import_plugin(bmd_importer)
		bmd_importer = null
	
	print("[MU Tools] Plugin unloaded")
