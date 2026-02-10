extends SceneTree

func _init():
	var data_path = "res://reference/MuMain/src/bin/Data/World1"
	var tex_loader = preload("res://addons/mu_tools/util/mu_texture_loader.gd")
	var parser_script = preload("res://addons/mu_tools/core/mu_terrain_parser.gd")
	var parser = parser_script.new()
	
	print("=== MU Texture Diagnostic ===")
	var test_textures = ["TileGrass01", "TileGround01", "TileRock01", "TileWater01"]
	for tex_name in test_textures:
		var path = data_path.path_join(tex_name + ".OZJ")
		var full_path = ProjectSettings.globalize_path(path)
		print("Testing: ", path)
		if FileAccess.file_exists(full_path):
			var tex = tex_loader.load_mu_texture(full_path)
			if tex:
				print("  SUCCESS: Loaded %dx%d" % [tex.get_width(), tex.get_height()])
			else:
				print("  FAILED: Could not load texture")
		else:
			print("  ERROR: File not found at ", full_path)
			
	print("\n=== MU Mapping Diagnostic ===")
	var map_path = data_path.path_join("EncTerrain1.map")
	if FileAccess.file_exists(ProjectSettings.globalize_path(map_path)):
		var map_data = parser.parse_mapping_file(ProjectSettings.globalize_path(map_path))
		if map_data:
			print("Map Number: ", map_data.map_number)
			print("Layer1 Size: ", map_data.layer1.size())
			var counts = {}
			for i in range(100): # Check first 100 tiles
				var val = map_data.layer1[i]
				counts[val] = counts.get(val, 0) + 1
			print("Sample indices (first 100): ", counts)
		else:
			print("FAILED to parse map file")
	
	quit()
