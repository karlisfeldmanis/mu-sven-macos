@tool
extends Node3D

## WorldLoader
## Orchestrates terrain loading via MUHeightmap and object spawning via MUObjectSpawner.

# class_name MUWorldLoader

const MUHeightmapClass = preload("res://addons/mu_tools/world/heightmap_node.gd")
const MUObjectSpawnerClass = preload("res://addons/mu_tools/world/object_spawner.gd")
const MUGrassSystemClass = preload("res://addons/mu_tools/world/grass_system.gd")

var _world_id: int = 1
var heightmap: MUHeightmapClass
var spawner: MUObjectSpawnerClass
var grass: MUGrassSystemClass

@export var world_id: int:
	get: return _world_id
	set(val):
		if _world_id == val: return
		_world_id = val
		if is_inside_tree():
			load_world(_world_id)

func load_world(id: int, data_path: String = "res://reference/MuMain/src/bin/Data"):
	_world_id = id
	print("[MUWorldLoader] Loading World %d..." % _world_id)
	
	# 1. Clear existing
	_clear_children()
	
	# 2. Setup Heightmap (Terrain)
	heightmap = MUHeightmapClass.new()
	heightmap.name = "Terrain"
	heightmap.world_id = id
	heightmap.data_path = data_path
	add_child(heightmap)
	
	# 3. Setup Spawner (Objects)
	spawner = MUObjectSpawnerClass.new()
	spawner.name = "Spawner"
	add_child(spawner)
	
	# 4. Trigger Object Loading
	# We remove the restrictive city filter to load ALL objects in the world
	var city_filter = Rect2() 
	
	# Use standard MUAPI to trigger the spawn via the node
	spawner.load_objects(
		self,
		heightmap.get_objects_data(),
		false, # show_debug
		heightmap.get_height_data(),
		false, # show_hidden
		id,
		city_filter
	)
	
	# 5. Setup Grass System
	grass = MUGrassSystemClass.new()
	grass.name = "GrassSystem"
	add_child(grass)
	grass.spawn_grass(heightmap)

	print("✓ World %d loaded successfully." % id)

func _clear_children():
	for child in get_children():
		child.queue_free()

func get_height_at(world_pos: Vector3) -> float:
	if heightmap:
		return heightmap.get_height_at_world(world_pos)
	return 0.0
