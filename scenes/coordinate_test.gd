extends Node3D

## Simple test scene to verify coordinate system and terrain+character integration

const MUCoordinates = preload("res://addons/mu_tools/core/mu_coordinate_utils.gd")
const MUTerrain = preload("res://addons/mu_tools/nodes/mu_terrain.gd")
const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/nodes/mesh_builder.gd")
const MUAnimationBuilder = preload("res://addons/mu_tools/core/animation_builder.gd")

var terrain: MUTerrain
var character_root: Node3D
var camera: Camera3D

var character_mesh: MeshInstance3D
var animation_player: AnimationPlayer

func _ready():
	print("\n=== Coordinate Test Scene ===\n")
	
	# Create nodes
	terrain = MUTerrain.new()
	terrain.world_id = 1
	add_child(terrain)
	
	character_root = Node3D.new()
	add_child(character_root)
	
	camera = Camera3D.new()
	add_child(camera)
	
	# Add lighting
	var light = DirectionalLight3D.new()
	light.position = Vector3(0, 10, 0)
	light.rotation_degrees = Vector3(-45, 0, 0)
	light.shadow_enabled = true
	add_child(light)
	
	# 1. Load terrain
	print("[1/4] Loading Lorencia terrain...")
	terrain.load_world()
	
	# 2. Load character
	print("[2/4] Loading Dark Knight...")
	_load_character()
	
	# 3. Position character at center of Lorencia (tile 128, 128)
	print("[3/4] Positioning character at tile (128, 128)...")
	var tile_pos = Vector2i(128, 128)
	var height = _get_terrain_height(tile_pos.x, tile_pos.y)
	var world_pos = MUCoordinates.tile_to_world(tile_pos.x, tile_pos.y, height)
	character_root.position = world_pos
	
	print("  Tile: ", tile_pos)
	print("  Height: %.3f m" % height)
	print("  World Position: ", world_pos)
	
	# 4. Setup camera
	print("[4/4] Setting up camera...")
	_setup_camera(world_pos)
	
	print("\n✓ Scene Ready!")
	print("  Character at: ", character_root.position)
	print("  Camera looking at: ", world_pos)

func _load_character():
	var bmd_path = "res://reference/MuMain/src/bin/Data/Player/DarkKnight.bmd"
	var parser = BMDParser.new()
	var bmd_data = parser.parse_file(bmd_path)
	
	if not bmd_data:
		push_error("Failed to load character BMD")
		return
	
	# Build mesh
	var mesh_builder = MUMeshBuilder.new()
	var mesh = mesh_builder.create_array_mesh(bmd_data)
	
	character_mesh = MeshInstance3D.new()
	character_mesh.mesh = mesh
	character_root.add_child(character_mesh)
	
	# Build animations
	var anim_builder = MUAnimationBuilder.new()
	animation_player = AnimationPlayer.new()
	character_root.add_child(animation_player)
	
	anim_builder.build_animations(bmd_data, animation_player, character_mesh.get_node("Skeleton3D"))
	
	# Apply Z-up to Y-up conversion
	character_mesh.rotation_degrees.x = -90
	
	# Play idle animation
	if animation_player.has_animation("Idle_1"):
		animation_player.play("Idle_1")

func _get_terrain_height(tile_x: int, tile_y: int) -> float:
	if not terrain or not terrain.heightmap:
		return 0.0
	
	var idx = MUCoordinates.tile_to_index(tile_x, tile_y)
	if idx >= 0 and idx < terrain.heightmap.size():
		return terrain.heightmap[idx]
	return 0.0

func _setup_camera(look_at_pos: Vector3):
	# MU camera settings from research
	camera.fov = 30.0
	camera.position = look_at_pos + Vector3(0, 0, 0)
	
	# Apply MU camera angles
	camera.rotation_degrees = Vector3(0, 0, 0)
	camera.rotate_x(deg_to_rad(-48.5))  # Pitch
	camera.rotate_y(deg_to_rad(-45.0))  # Yaw
	camera.translate(Vector3(0, 0, 10.0))  # Distance
	
	# Look at character's chest
	var target = look_at_pos + Vector3(0, 0.9, 0)
	camera.look_at(target, Vector3.UP)
