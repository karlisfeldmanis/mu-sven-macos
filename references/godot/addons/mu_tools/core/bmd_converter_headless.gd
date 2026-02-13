# class_name BMDConverterHeadless

## Headless BMD Converter
##
## Converts MuOnline .bmd files to Godot resources without editor dependencies

const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/rendering/bmd_mesh_builder.gd")

func convert_file(input_path: String, output_dir: String) -> Dictionary:
	var result = {
		"success": false,
		"mesh_count": 0,
		"bone_count": 0,
		"action_count": 0,
		"output_files": []
	}
	
	# Parse BMD file
	var parser = BMDParser.new()
	if not parser.parse_file(input_path, true):
		push_error("Failed to parse BMD file: " + input_path)
		return result
	
	var base_name = input_path.get_file().get_basename()
	
	# Convert meshes
	for i in range(parser.get_mesh_count()):
		var bmd_mesh = parser.get_mesh(i)
		var mesh = MUMeshBuilder.build_mesh(bmd_mesh, null, "", null, true)
		
		if mesh:
			var mesh_path = output_dir.path_join(base_name + "_mesh_" + str(i) + ".res")
			var err = ResourceSaver.save(mesh, mesh_path)
			
			if err == OK:
				result.output_files.append(mesh_path)
				result.mesh_count += 1
				print("  ✓ Saved mesh: ", mesh_path)
			else:
				push_error("Failed to save mesh: " + mesh_path)
	
	# Convert skeleton (Phase 3 - TODO)
	if parser.get_bone_count() > 0:
		print("  ⚠ Skeleton conversion not yet implemented (Phase 3)")
		result.bone_count = parser.get_bone_count()
	
	# Convert animations (Phase 4 - TODO)
	if parser.actions.size() > 0:
		print("  ⚠ Animation conversion not yet implemented (Phase 4)")
		result.action_count = parser.actions.size()
	
	result.success = result.mesh_count > 0
	return result
