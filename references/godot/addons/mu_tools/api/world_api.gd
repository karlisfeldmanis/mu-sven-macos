@tool
extends Node

## WorldAPI
## Provides access to high-level world loading and management.

const WorldLoaderClass = preload("res://addons/mu_tools/world/world_loader.gd")

var _loader: WorldLoaderClass

func get_loader(root: Node) -> WorldLoaderClass:
	if not _loader or not _loader.is_inside_tree():
		_loader = WorldLoaderClass.new()
		_loader.name = "MUWorld"
		root.add_child(_loader)
	return _loader

func load_world(root: Node, id: int, data_path: String = "res://reference/MuMain/src/bin/Data"):
	var loader = get_loader(root)
	loader.load_world(id, data_path)
	return loader
