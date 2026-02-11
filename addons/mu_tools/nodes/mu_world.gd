@tool
extends Node3D

class_name MUWorld

const MUObjectManagerClass = preload("res://addons/mu_tools/nodes/mu_object_manager.gd")

## Unified controller for MU world maps.
## Coordinates terrain, objects, environment, and height queries.

@export var world_id: int = 1:
	set(val):
		world_id = val
		if is_inside_tree() and not Engine.is_editor_hint():
			load_world()

@export var data_path: String = "res://reference/MuMain/src/bin/Data":
	set(val):
		data_path = val
		if is_inside_tree() and not Engine.is_editor_hint():
			load_world()

@export var environment_type: int = 0
@export var show_objects: bool = true
@export var filter_rect: Rect2 = Rect2() # Godot World Space (XZ)

var _api: Node
var _heightmap: MUHeightmap
var _object_spawner: Node3D # Will be MUObjectSpawner/Manager
var _environment: MUEnvironment

var _world_loaded: bool = false
var _height_data: PackedFloat32Array

func _init():
	_api = load("res://addons/mu_tools/core/mu_api.gd").new()

func _ready():
	if not Engine.is_editor_hint():
		load_world()

func load_world():
	print("[MUWorld] Loading World %d..." % world_id)
	_cleanup()
	
	# 1. Heightmap — set properties before add_child so _ready()
	# loads the correct world (avoids spurious double-load)
	_heightmap = MUHeightmap.new()
	_heightmap.name = "Heightmap"
	_heightmap.data_path = data_path
	_heightmap.world_id = world_id
	add_child(_heightmap)
	
	# Wait for heightmap data (HACK: usually synchronous but let's be safe)
	# MUHeightmap.load_heightmap() is called in its _ready or setter
	_height_data = _heightmap.get_height_data()
	
	# 2. Objects
	if show_objects:
		_object_spawner = MUObjectManagerClass.new()
		_object_spawner.name = "ObjectManager"
		add_child(_object_spawner)
		
		var world_data = _api.data().load_world_data(world_id, data_path)
		if not world_data.is_empty() and world_data.has("objects"):
			MUObjectManagerClass.load_objects(_object_spawner, world_data.objects, false, _height_data, false, world_id, filter_rect)
	else:
		print("[MUWorld] Object rendering disabled.")
	
	# 3. Environment (Managed by MUHeightmap usually, but we could unify here)
	# Currently MUHeightmap.load_heightmap() calls _api.render().setup_world_environment()
	
	_world_loaded = true
	print("✓ MUWorld: World %d Loaded." % world_id)

func get_height_at(world_pos: Vector3) -> float:
	if not _heightmap: return 0.0
	return _heightmap.get_height_at_world(world_pos)

func _cleanup():
	if _heightmap: _heightmap.queue_free(); _heightmap = null
	if _object_spawner: _object_spawner.queue_free(); _object_spawner = null
	_world_loaded = false
