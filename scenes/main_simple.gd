extends Node3D

const MUTerrainSimple = preload("res://addons/mu_tools/mu_terrain_simple.gd")
const MUStateMachine = preload("res://addons/mu_tools/mu_state_machine.gd") 
const MULogger = preload("res://addons/mu_tools/mu_logger.gd")
const MUBMDRegistry = preload("res://addons/mu_tools/mu_bmd_registry.gd")

var _terrain: MUTerrainSimple
var _character: Node3D
var _camera_rig: Node3D
var _camera: Camera3D
var _state_machine: MUStateMachine

# Camera Settings
var _yaw: float = 45.0 # PERFECT: Confirmed by User
var _pitch: float = -48.5 # Sven Standard
var _zoom: float = 10.5 # Sven Standard
var _camera_offset: Vector3 = Vector3.ZERO

# Persistence
var _save_timer: float = 0.0
var _last_saved_pos: Vector3 = Vector3.ZERO
const SAVE_INTERVAL: float = 2.0
const SAVE_PATH: String = "user://savegame.json"

func _ready():
	print("[MainSimple] Starting simplified engine...")
	
	# 1. Setup Lighting
	_setup_lighting()
	
	_setup_camera()
	_load_terrain()
	_spawn_character()
	
	print("[MainSimple] Ready. Tree Nodes: ", get_tree().get_node_count())
	
	print("[MainSimple] Ready. Tree Nodes: ", get_tree().get_node_count())
	for child in get_children():
		print(" - ", child.name, " (", child.get_class(), ")")
	
	# 5. Setup Navigation (Placeholder for now, or just move logic)
	_setup_navigation_placeholder()

func _setup_lighting():
	# Clean up any existing lights
	for child in get_children():
		if child is DirectionalLight3D:
			child.queue_free()
	
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.rotation_degrees = Vector3(-60, 45, 0)
	sun.light_energy = 1.0 # Standard energy
	sun.light_indirect_energy = 0.5
	sun.shadow_enabled = true
	
	# Performance optimized settings (High Quality Defaults)
	# Use SHADOW_ORTHOGONAL for stable, artifact-free shadows in isometric view
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL 
	sun.directional_shadow_max_distance = 120.0 
	
	# SHADOW TUNING:
	# Persistent "sines" -> Needs high Normal Bias or Bias.
	sun.shadow_blur = 1.0 
	sun.shadow_bias = 0.08 # Heavy bias
	sun.shadow_normal_bias = 4.0 # High normal bias for orthogonal terrain
	
	# Rotate Sun to cast shadow SIDEWAYS (Cross Lighting)
	sun.rotation_degrees.y = 45.0 
	sun.rotation_degrees.x = -45.0
	
	add_child(sun)
	print("[MainSimple] Created optimized Sun (Orthogonal)")
	
	_setup_environment()

func _setup_environment() -> void:
	# Clean up existing environment
	for child in get_children():
		if child is WorldEnvironment:
			child.queue_free()
			
	# 1. Environment & Background
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK 
	env.fog_aerial_perspective = 0.5
	env.fog_sky_affect = 0.2
	
	# Global Illumination & Background
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(30.0/255.0, 20.0/255.0, 10.0/255.0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.2, 0.2, 0.2)
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	
	var world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)
	print("[MainSimple] Environment configured (Lorencia Fog)")

func _setup_camera():
	_camera_rig = Node3D.new()
	_camera_rig.name = "CameraRig"
	add_child(_camera_rig)
	
	var pitch_node = Node3D.new()
	pitch_node.name = "Pitch"
	_camera_rig.add_child(pitch_node)
	
	_camera = Camera3D.new()
	_camera.name = "MainCamera"
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.fov = 30.0 # Sven Standard
	_camera.current = true 
	_camera.position.z = _zoom 
	_camera.near = 0.5
	_camera.far = 100.0 
	
	pitch_node.add_child(_camera)
	
	_camera_rig.rotation_degrees.y = _yaw
	pitch_node.rotation_degrees.x = _pitch
	
	print("[MainSimple] Camera Setup: Pitch=%.1f, Yaw=%.1f, Zoom=%.1f" % [_pitch, _yaw, _zoom])

func _load_terrain():
	_terrain = MUTerrainSimple.new()
	_terrain.name = "Terrain"
	_terrain.world_id = 0
	# Point to reference data
	_terrain.data_path = "res://reference/MuMain/src/bin/Data" 
	add_child(_terrain)

func _spawn_character():
	# Use existing character loader logic or simplified
	# We need a character scene or just a mesh.
	# Let's try to reuse `main.gd`'s `load_character_with_state_machine` logic but simplified
	
	print("[MainSimple] Spawning character...")
	
	# Create Character Node
	_character = Node3D.new()
	_character.name = "Player"
	_character.add_to_group("player_character") # For Terrain logic
	# Reverted to 1.0 to ensure stability
	_character.scale = Vector3(1.0, 1.0, 1.0) 
	add_child(_character)
	
	# Load Skeleton from player.bmd (Using Registry)
	var bmd_path = "res://raw_data/Player/player.bmd"
	var parser = MUBMDRegistry.get_bmd(bmd_path)
	
	if parser:
		# We need a Skeleton3D. BMDParser usually doesn't return a skeleton directly suitable for Node3D tree easily
		# without the MUMeshBuilder or similar.
		# Let's use the SkeletonBuilder if available?
		var MUSkeletonBuilder = preload("res://addons/mu_tools/skeleton_builder.gd")
		# Fix: Pass arrays, not parser object directly
		var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions) 
		if skeleton:
			# Match legacy structure EXACTLY:
			# Character -> MUContainer -> [Skeleton, AnimationPlayer]
			var mu_container = Node3D.new()
			mu_container.name = "MUContainer"
			mu_container.rotation_degrees.x = 0.0
			
			# FIX: Sane offset to center mesh visually
			# Visual: Red dot is "Up-Right" (North-East).
			# Moving Mesh North-East (World +X, -Z).
			# Reduced from 5.0 to 1.2 to keep on screen.
			# Reset visual offset to zero to prevent rotation swing
			mu_container.position = Vector3.ZERO 
			
			_character.add_child(mu_container)
			
			
			skeleton.name = "Skeleton" 
			mu_container.add_child(skeleton)
			
			# Ensure meshes cast shadows
			for child in skeleton.get_children():
				if child is MeshInstance3D:
					child.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
			
			# Setup AnimationPlayer (Sibling in MUContainer)
			var anim_player = AnimationPlayer.new()
			anim_player.name = "AnimationPlayer"
			mu_container.add_child(anim_player) 
			
			var AnimationBuilder = preload("res://addons/mu_tools/animation_builder.gd")
			var library = AnimationBuilder.build_animation_library(bmd_path, parser.actions, skeleton)
			anim_player.add_animation_library("", library)
			print("[MainSimple] Built AnimationLibrary with %d animations" % library.get_animation_list().size())
			# Attach parts?
			_attach_part(skeleton, "res://raw_data/Player/ArmorClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/PantClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/HelmClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/GloveClass01.bmd")
			_attach_part(skeleton, "res://raw_data/Player/BootClass01.bmd")
			
			# Setup State Machine
			_state_machine = MUStateMachine.new()
			_state_machine.name = "StateMachine"
			_state_machine.weapon_class = "Sword" # Set weapon class
			add_child(_state_machine)
			
			# Wait for terrain to be ready if needed (usually _ready runs fast, but safely..)
			# Ensure terrain has heightmap loaded (it loads in _ready which is good)
			_state_machine.setup(anim_player, bmd_path, _character, _terrain)
			_state_machine.set_state(MUStateMachine.State.IDLE)
			_state_machine.update_animation() # Force initial animation
			
			# Position (Persistence or Default)
			# var saved_pos = _load_state() # Disabled for debugging
			# Default: Lorencia Center (Hardcoded for debugging)
			var spawn_tile_x = 127
			var spawn_tile_y = 127
			var h = _get_height_at(float(spawn_tile_x) + 0.5, float(spawn_tile_y) + 0.5)
			_character.position = Vector3(
				float(spawn_tile_x) + 0.5, 
				h, 
				float(spawn_tile_y) + 0.5
			)
			print("[MainSimple] DEBUG: Spawned at: ", _character.position)
			
			_last_saved_pos = _character.position
			
			# Explicitly update state machine pos
			_state_machine.snap_to_terrain()
			_state_machine.target_position = _character.position
			_state_machine.is_moving = false # STOP MOVEMENT
			
			# Force camera to snap immediately
			_camera_rig.position = _character.position
			_camera_rig.position.y += 1.5
	else:
		print("[MainSimple] Failed to load player BMD")

func _attach_part(skeleton, path):
	# Simplified attachment reuse
	var MUMB = preload("res://addons/mu_tools/mesh_builder.gd")
	var p = MUBMDRegistry.get_bmd(path)
	if p:
		for mesh_idx in range(p.get_mesh_count()):
			var b_mesh = p.get_mesh(mesh_idx)
			var mi = MUMB.create_mesh_instance(b_mesh, skeleton, path, p, true)
			if mi:
				skeleton.add_child(mi)

func _get_height_at(world_x: float, world_z: float) -> float:
	if not _terrain or not _terrain.heightmap: return 0.0
	
	# World to Local (Identity 1.0 but offset by 0.5? No, tiles matches world meters)
	# Check coordinate_utils.gd: tile_to_world(x, y) -> (x+0.5, h, y+0.5)
	# So a world pos of (x.5, z.5) corresponds to the CENTER of tile (x, z).
	# But the mesh vertices are at integers (0,0 to 256,256).
	# So world (0,0) is vertex (0,0).
	# We can use direct world coordinates.
	
	var x = world_x
	var z = world_z
	
	# Clamp to terrain bounds
	if x < 0 or x >= 255 or z < 0 or z >= 255:
		return 0.0
		
	var fx = floor(x)
	var fz = floor(z)
	
	var tx = int(fx)
	var tz = int(fz)
	
	# Get fractional part within the cell [0,1]
	var dx = x - fx
	var dz = z - fz
	
	# Get heights of the 4 corners
	var h00 = _get_raw_height(tx, tz)
	var h10 = _get_raw_height(tx + 1, tz)
	var h01 = _get_raw_height(tx, tz + 1)
	var h11 = _get_raw_height(tx + 1, tz + 1)
	
	# Triangulation pattern from mu_terrain_mesh_builder.gd:
	# Diagonal goes from (0,0) to (1,1).
	# Tri 1: (0,0), (1,1), (1,0) -> Bottom/Right side (dx > dz)
	# Tri 2: (0,0), (0,1), (1,1) -> Top/Left side (dz > dx)
	# WAIT: mu_terrain_mesh_builder.gd lines 54-67:
	# Tri 1: v00, v11, v10. Vertices: (0,0,H00), (1,1,H11), (1,0,H10).
	# This covers the area where we move from (0,0) towards (1,0) and (1,1).
	# This implies the diagonal is (0,0)->(1,1).
	
	var final_h = 0.0
	
	if dx > dz:
		# Triangle 1: (0,0)-(1,0)-(1,1)
		# Barycentric or simple plane interpolation.
		# Plane defined by H00, H10, H11.
		# height = H00 + (H10-H00)*dx + (H11-H10)*dz ?
		# Let's check:
		# at dx=0, dz=0 -> H00. Correct.
		# at dx=1, dz=0 -> H10. Correct.
		# at dx=1, dz=1 -> H00 + (H10-H00) + (H11-H10) = H11. Correct.
		final_h = h00 + (h10 - h00) * dx + (h11 - h10) * dz
	else:
		# Triangle 2: (0,0)-(0,1)-(1,1)
		# Plane defined by H00, H01, H11.
		# height = H00 + (H11-H01)*dx + (H01-H00)*dz
		# Check:
		# at dx=0, dz=0 -> H00. Correct.
		# at dx=0, dz=1 -> H01. Correct.
		# at dx=1, dz=1 -> H00 + H11 - H01 + H01 - H00 = H11. Correct.
		final_h = h00 + (h11 - h01) * dx + (h01 - h00) * dz
		
	# Vertical Bias to prevent sinking on edges (+5cm)
	return final_h + 0.05

func _get_raw_height(tx: int, ty: int) -> float:
	var idx = ty * 256 + tx
	if idx >= 0 and idx < _terrain.heightmap.size():
		return _terrain.heightmap[idx] * 1.5 * 0.01
	return 0.0

func _process(delta):
	if _character:
		# Persistence: Autosave
		_save_timer += delta
		if _save_timer >= SAVE_INTERVAL:
			_save_timer = 0.0
			if _character.position.distance_to(_last_saved_pos) > 0.1:
				_save_state()
				
		# Follow Cam (Direct snap to verify centering)
		var t = _character.position
		t.y += 1.5
		
		# Apply Camera Offset to shift character on screen
		# To put Char at TOP, we must look "Below" them (South/East).
		# Current offset (2.5, 0, -2.5) moves target North-West (Opposite).
		# Let's flip it to move target South-East.
		# But wait, looking at `coordinate_test.gd`: it looks at (0, 0.9, 0).
		# If user wants Char at Top, we need to look at (0, 0.9, 0) + Shift.
		# Shift direction should be "Down Screen".
		# Screen Down = +Z +X (approx).
		# Let's try ZERO offset first (Centered), then tweak.
		# User said "Top Side", default is Center.
		# Center usually puts char in middle. "Top Side" means char is above middle.
		# So Camera Center must be BELOW Char.
		# So Target = Char + (DownScreenVector * Amount).
		
		# Let's try applying the defined `_camera_offset`.
		# Current value (2.5, 0, -2.5) looks North-West. Wrong way?
		# Let's assume standard behavior first.
		_camera_rig.position = t + _camera_offset

	# AUTO ORBIT REMOVED
	
	if Engine.get_frames_drawn() % 60 == 0:
		_camera.make_current() # FORCE
		print("[MainSimple] Player Pos: ", _character.global_position, " Rig Pos: ", _camera_rig.global_position)
	
	_handle_input(delta)

func _setup_navigation_placeholder():
	# For now, simplistic click-to-move is handled by StateMachine's move_to if we hook inputs.
	pass

func _handle_input(delta):
	# Navigation
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		var mouse_pos = get_viewport().get_mouse_position()
		var from = _camera.project_ray_origin(mouse_pos)
		var to = from + _camera.project_ray_normal(mouse_pos) * 2000
		
		# Raycast against Terrain only (Collision Mask 1 implicitly or explicit)
		# NOTE: MUTerrainSimple adds a MeshInstance3D with generic collision? 
		# Actually MUTerrainSimple creates a MeshInstance3D "TerrainMesh".
		# We need to ensure it has collision or we raycast against a plane.
		# For now, let's assume direct_space_state works if we add collision, 
		# OR use a plane intersection fallback if missing.
		
		# Fallback: Raycast to Plane(Vector3.UP, 0) since MU is flat-ish
		var plane = Plane(Vector3.UP, 0)
		var intersection = plane.intersects_ray(from, to - from)
		
		if intersection:
			if _state_machine:
				_state_machine.move_to(intersection)
		
		# Try physics raycast too just in case terrain has collider
		var space = get_world_3d().direct_space_state
		var query = PhysicsRayQueryParameters3D.create(from, to)
		var res = space.intersect_ray(query)
		if res and _state_machine:
			_state_machine.move_to(res.position)
	
	# Camera Settings Hotkeys (Debug)
	if Input.is_key_pressed(KEY_COMMA): # Decrease Zoom (Zoom IN)
		_zoom = max(100.0, _zoom - 100.0 * delta)
		_camera.size = _zoom
		print("Zoom: ", _zoom)
	if Input.is_key_pressed(KEY_PERIOD): # Increase Zoom (Zoom OUT)
		_zoom = min(500.0, _zoom + 100.0 * delta)
		_camera.position.z = _zoom
		print("Zoom: ", _zoom)
	
	# Rotation Control
	if Input.is_key_pressed(KEY_Q): _yaw -= 90.0 * delta
	if Input.is_key_pressed(KEY_E): _yaw += 90.0 * delta
	if Input.is_key_pressed(KEY_R): _pitch += 45.0 * delta
	if Input.is_key_pressed(KEY_F): _pitch -= 45.0 * delta
	
	_camera_rig.rotation_degrees.y = _yaw
	_camera_rig.get_node("Pitch").rotation_degrees.x = _pitch


func _notification(what):
	if what == NOTIFICATION_WM_CLOSE_REQUEST or what == NOTIFICATION_WM_GO_BACK_REQUEST:
		if _character:
			_save_state()

func _save_state():
	if not _character: return
	
	var data = {
		"x": _character.position.x,
		"y": _character.position.y,
		"z": _character.position.z
	}
	
	var file = FileAccess.open(SAVE_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data))
		_last_saved_pos = _character.position
		# print("[MainSimple] Saved state: ", _character.position)

func _load_state() -> Vector3:
	if not FileAccess.file_exists(SAVE_PATH):
		return Vector3.ZERO
		
	var file = FileAccess.open(SAVE_PATH, FileAccess.READ)
	if not file: return Vector3.ZERO
	
	var content = file.get_as_text()
	var json = JSON.new()
	if json.parse(content) == OK:
		var data = json.get_data()
		if data and data.has("x") and data.has("y") and data.has("z"):
			return Vector3(data["x"], data["y"], data["z"])
	
	return Vector3.ZERO

