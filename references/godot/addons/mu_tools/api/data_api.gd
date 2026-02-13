extends Node
# MUDataAPI.gd
# Interface for data parsing and retrieval.

const MUTerrainParser = preload("res://addons/mu_tools/parsers/terrain_parser.gd")
const MUFileUtil = preload("res://addons/mu_tools/core/file_util.gd")

func load_world_data(world_id: int, base_path: String) -> Dictionary:
	var parser = MUTerrainParser.new()
	var world_dir = "World" + str(world_id + 1)
	var world_path = base_path.path_join(world_dir)
	
	if base_path.get_file() == world_dir:
		world_path = base_path
	
	var data = {
		"heightmap": parser.parse_height_file(world_path.path_join("TerrainHeight.OZB")),
		"mapping": _load_mapping(parser, world_path, world_id),
		"objects": parser.parse_objects_file(
			world_path.path_join("EncTerrain" + str(world_id + 1) + ".obj"),
			world_id),
		"attributes": parser.parse_attributes_file(world_path.path_join("EncTerrain" + str(world_id + 1) + ".att"))
	}
	return data

func _load_mapping(parser, world_path, world_id):
	var mapping_file = world_path.path_join("EncTerrain" + str(world_id + 1) + ".map")
	var is_encrypted = true
	if not FileAccess.file_exists(mapping_file):
		var fb = world_path.path_join("Terrain.map")
		if FileAccess.file_exists(fb):
			mapping_file = fb
			is_encrypted = false
	return parser.parse_mapping_file(mapping_file, is_encrypted)

func get_tile_info(map_data, x: int, y: int) -> Dictionary:
	var idx = y * 256 + x
	if idx >= map_data.layer1.size(): return {}
	return {
		"layer1": map_data.layer1[idx],
		"layer2": map_data.layer2[idx],
		"alpha": map_data.alpha[idx],
		"is_water": Vector2i(x, y) in map_data.water_tiles
	}
