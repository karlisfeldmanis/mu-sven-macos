@tool
extends Node

class_name MUAssetAPI

## Centralized API for MU Online asset management.
## Orchestrates parsing, exporting, and runtime instantiation.

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUOBJExporter = preload("res://addons/mu_tools/core/mu_obj_exporter.gd")
const MUObjLoader = preload("res://addons/mu_tools/core/mu_obj_loader.gd")

## Imports a BMD file to OBJ/MTL in the specified output directory.
static func import_asset(bmd_path: String, output_dir: String) -> bool:
	var parser = BMDParser.new()
	if not parser.parse_file(bmd_path, false):
		push_error("[MUAssetAPI] Failed to parse BMD: " + bmd_path)
		return false
		
	if not MUOBJExporter.export_bmd(parser, bmd_path, output_dir):
		push_error("[MUAssetAPI] Failed to export OBJ: " + bmd_path)
		return false
		
	return true

## Instantiates a model (OBJ) as a MeshInstance3D with MU-specific materials.
static func instantiate_asset(obj_path: String) -> MeshInstance3D:
	var mi = MUObjLoader.build_mesh_instance(obj_path)
	if not mi:
		push_error("[MUAssetAPI] Failed to load asset: " + obj_path)
	return mi

## Bulk validates a list of BMD files against their exported counterparts.
## Returns a report of success/failures.
static func bulk_validate_assets(bmd_paths: Array[String], export_dir: String) -> Dictionary:
	var report = {
		"total": bmd_paths.size(),
		"success": 0,
		"failure": 0,
		"details": []
	}
	
	for bmd in bmd_paths:
		var base = bmd.get_file().get_basename()
		var obj = export_dir.path_join(base + ".obj")
		if FileAccess.file_exists(obj):
			report["success"] += 1
		else:
			report["failure"] += 1
			report["details"].append(base)
			
	return report
