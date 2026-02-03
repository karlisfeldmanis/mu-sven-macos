extends Node3D

## Visual Rendering Test
## Loads BMD with skeleton + meshes for visual inspection

@export_file("*.bmd") var bmd_file_path: String = "res://raw_data/Player/ArmorClass01.bmd"

var _anim_player: AnimationPlayer
var _current_action: int = 0
var _state_machine: MUStateMachine

const MUCoordinates = preload("res://addons/mu_tools/mu_coordinates.gd")
const MUAnimationRegistry = preload("res://addons/mu_tools/mu_animation_registry.gd")
const MUStateMachine = preload("res://addons/mu_tools/mu_state_machine.gd")

var _character: Node3D
var _terrain: Node # MUTerrain instance

# Camera Settings (Authentic MU Settings)
var _camera_yaw: Node3D
var _camera_pitch: Node3D
var _camera: Camera3D

var _pitch_val: float = -48.5
var _yaw_val: float = -45.0
var _distance_val: float = 25.0 # Zoomed out more to see terrain
var _fov_val: float = 30.0 # Authentic narrow perspective

func _ready() -> void:
	_setup_camera()
	_add_lorencia_terrain()
	load_character_with_state_machine(bmd_file_path)
	
	# Position character at Lorencia city (around the safe zone)
	var tile_pos = Vector2i(130, 116)
	var height = _get_terrain_height(tile_pos.x, tile_pos.y)
	var world_pos = MUCoordinates.tile_to_world(tile_pos.x, tile_pos.y, height)
	_character.position = world_pos
	
	# Update camera to center on character
	_update_camera_rig()
	
	print("[Render Test] Character positioned at tile (%d, %d)" % [tile_pos.x, tile_pos.y])
	print("  World Position: ", world_pos)
	print("  Terrain Height: %.3f m" % height)

func _add_lorencia_terrain() -> void:
	var mut_script = load("res://addons/mu_tools/mu_terrain.gd")
	_terrain = mut_script.new()
	_terrain.world_id = 1
	_terrain.data_path = "res://reference/MuMain/src/bin/Data/World1"
	add_child(_terrain)
	_terrain.load_world()

func _get_terrain_height(tile_x: int, tile_y: int) -> float:
	if not _terrain or not _terrain.heightmap:
		return 0.0
	
	var idx = MUCoordinates.tile_to_index(tile_x, tile_y)
	if idx >= 0 and idx < _terrain.heightmap.size():
		return _terrain.heightmap[idx]
	return 0.0

func _setup_camera() -> void:
	# Clean up any existing cameras in the scene
	for child in get_children():
		if child is Camera3D: child.queue_free()
		if child.name.begins_with("Camera"): child.queue_free()

	_camera_yaw = Node3D.new()
	_camera_yaw.name = "CameraYaw"
	add_child(_camera_yaw)
	
	_camera_pitch = Node3D.new()
	_camera_pitch.name = "CameraPitch"
	_camera_yaw.add_child(_camera_pitch)
	
	_camera = Camera3D.new()
	_camera.name = "MainCamera"
	_camera.current = true # CRITICAL: Ensure this camera is used
	_camera_pitch.add_child(_camera)
	
	_update_camera_rig()

func _update_camera_rig() -> void:
	# Lock to MU Native Angles
	_camera_yaw.rotation_degrees.y = _yaw_val
	_camera_pitch.rotation_degrees.x = _pitch_val
	
	# Narrow perspective creates the "almost isometric" look seen in reference
	_camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	_camera.fov = _fov_val
	_camera.position.z = _distance_val
	
	# Position camera rig at character location to center them in view
	if _character:
		var char_pos = _character.position
		_camera_yaw.position = char_pos
	else:
		# Default to center of Lorencia
		_camera_yaw.position = Vector3(128.5, 0, 128.5)

func _process(_delta: float) -> void:
	pass

func _on_speed_changed(val: float) -> void:
	if _anim_player:
		_anim_player.speed_scale = val



func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		if event.keycode >= KEY_0 and event.keycode <= KEY_9:
			var action_idx = event.keycode - KEY_0
			play_action(action_idx)
		
		# Simulation shortcuts
		if event.keycode == KEY_W:
			_cycle_weapon()
		if event.keycode == KEY_S:
			_toggle_safe_zone()

func _cycle_weapon() -> void:
	var weapons = ["None", "Sword", "TwoHandSword", "Spear", "Bow", "Staff"]
	var idx = weapons.find(_state_machine.weapon_class)
	_state_machine.weapon_class = weapons[(idx + 1) % weapons.size()]
	_state_machine.update_animation()

func _toggle_safe_zone() -> void:
	_state_machine.is_safe_zone = !_state_machine.is_safe_zone
	_state_machine.update_animation()

func play_action(idx: int) -> void:
	if not _anim_player: return
	var anim_name = MUAnimationRegistry.get_action_name(bmd_file_path, idx)
	if _anim_player.has_animation(anim_name):
		_anim_player.play(anim_name)
		_current_action = idx
		
		# Reset to 1.0x (now authentic because speeds are baked)
		_on_speed_changed(1.0)
		
		print("▶ Switching to: ", anim_name, " (Speed: 1.0x - Authentic)")
	else:
		# Fallback to index if mapping fails or list is short
		var fallback = "Action_%d" % idx
		if _anim_player.has_animation(fallback):
			_anim_player.play(fallback)
			print("▶ Switching to (fallback): ", fallback)

func load_character_with_state_machine(path: String) -> void:
	print("Loading Character with State Machine...")
	
	# For player models, the skeleton and animations are in player.bmd
	var base_path = path
	if "player/" in path.to_lower():
		base_path = "res://raw_data/Player/player.bmd"
	
	var skeleton = load_base_skeleton(base_path)
	if not skeleton:
		return
		
	# If the original path was a part (like Armor), attach it too
	if path != base_path:
		attach_bmd_meshes(path, skeleton)
		
	# Load standard armor parts for visualization
	var parts = [
		"res://raw_data/Player/ArmorClass01.bmd",
		"res://raw_data/Player/PantClass01.bmd",
		"res://raw_data/Player/GloveClass01.bmd",
		"res://raw_data/Player/BootClass01.bmd",
		"res://raw_data/Player/HelmClass01.bmd"
	]
	
	for part_path in parts:
		if part_path != path and FileAccess.file_exists(part_path):
			attach_bmd_meshes(part_path, skeleton)
	
	# Add skeleton visualization (Hidden for clean view)
	# _add_skeleton_visualization(skeleton)
	# Apply MU Native Class Scale (Default 1.2x for DK/general)
	# Reference: ZzzCharacter.cpp -> SetCharacterScale
	_character.scale = Vector3(1.2, 1.2, 1.2)
	
	# Setup State Machine
	_state_machine = MUStateMachine.new()
	add_child(_state_machine)
	_state_machine.setup(_anim_player, base_path)
	_state_machine.weapon_class = "None" # Start clean
	_state_machine.set_state(MUStateMachine.State.IDLE)
	_state_machine.update_animation()
	
	# Measure total height for scale verification
	var aabb = _get_total_aabb(skeleton)
	print("\n[Scale Verification] Character height: %.2fm" % aabb.size.y)
	
	print("\n=== Character loaded and idling! ===")

func _get_total_aabb(node: Node3D) -> AABB:
	var total_aabb = AABB()
	var first = true
	
	# Recursively find all MeshInstances
	var mesh_nodes = []
	var skeleton: Skeleton3D = null
	var stack = [node]
	while not stack.is_empty():
		var n = stack.pop_back()
		if n is MeshInstance3D and n.mesh:
			mesh_nodes.append(n)
		if n is Skeleton3D:
			skeleton = n
		for child in n.get_children():
			stack.append(child)
	
	# Force update to ensure bone poses are calculated
	if skeleton:
		var bone_heights = []
		# Measure height using bone rest positions
		for i in range(skeleton.get_bone_count()):
			var bone_rest_global = skeleton.get_bone_global_rest(i)
			var bone_pos = skeleton.global_transform * bone_rest_global.origin
			bone_heights.append(bone_pos.y)
			if first:
				total_aabb = AABB(bone_pos, Vector3.ZERO)
				first = false
			else:
				total_aabb = total_aabb.expand(bone_pos)
	
	return total_aabb

func _add_ref_post(pos: Vector3, text: String, height: float, color: Color) -> void:
	var mesh_instance = MeshInstance3D.new()
	var cylinder = CylinderMesh.new()
	cylinder.top_radius = 0.05
	cylinder.bottom_radius = 0.05
	cylinder.height = height
	mesh_instance.mesh = cylinder
	mesh_instance.position = pos + Vector3(0, height/2.0, 0)
	var mat = StandardMaterial3D.new()
	mat.albedo_color = color
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mesh_instance.material_override = mat
	add_child(mesh_instance)
	
	var label = Label3D.new()
	label.text = text
	label.position = pos + Vector3(0, height + 0.2, 0)
	label.pixel_size = 0.005
	add_child(label)


func load_base_skeleton(path: String) -> Skeleton3D:
	var parser = BMDParser.new()
	if not parser.parse_file(path, false): # debug=false for clean view
		push_error("Failed to parse base BMD: " + path)
		return null
	
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	if not skeleton:
		push_error("Failed to build skeleton")
		return null
		
	# Create character root with Z-up to Y-up rotation
	_character = Node3D.new()
	_character.name = "Character"
	# Instead of rotating the character root which messes with local movement,
	# we wrap the skeleton in a container that handles the MU-to-Godot rotation.
	var mu_container = Node3D.new()
	mu_container.name = "MUContainer"
	mu_container.rotation.x = -PI / 2.0
	_character.add_child(mu_container)
	add_child(_character)
	
	skeleton.name = "Skeleton"
	mu_container.add_child(skeleton)
	
	# 1. Setup AnimationPlayer
	_anim_player = AnimationPlayer.new()
	_anim_player.name = "AnimationPlayer"
	mu_container.add_child(_anim_player)
	
	# 2. Build Animations
	const AnimationBuilder = preload("res://addons/mu_tools/animation_builder.gd")
	var library = AnimationBuilder.build_animation_library(path, parser.actions, skeleton)
	_anim_player.add_animation_library("", library)
	
	# 3. Add meshes for the base model (e.g. skin, head)
	for bmd_mesh in parser.meshes:
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, path)
		if mesh_instance:
			skeleton.add_child(mesh_instance)
	
	# 4. Default Idle Loop (Action 1 = Stop_Male)
	var idle_name = MUAnimationRegistry.get_action_name(path, 1)
	if _anim_player.has_animation(idle_name):
		_anim_player.play(idle_name)
		_current_action = 1
	
	return skeleton

func attach_bmd_meshes(path: String, skeleton: Skeleton3D) -> void:
	var parser = BMDParser.new()
	if not parser.parse_file(path, false): # debug=false
		return
		
	for i in range(parser.meshes.size()):
		var bmd_mesh = parser.meshes[i]
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, skeleton, path)
		if mesh_instance:
			mesh_instance.name = path.get_file().get_basename() + "_%d" % i
			skeleton.add_child(mesh_instance)
			
			# Explicitly set skeleton path AFTER adding to tree
			mesh_instance.skeleton = mesh_instance.get_path_to(skeleton)
			if mesh_instance.mesh and mesh_instance.get_surface_override_material(0):
				print("  [Mesh %d] Attached with material: %s" % [i, mesh_instance.get_surface_override_material(0).get_shader_parameter("albedo_texture").resource_path.get_file()])

func _add_skeleton_visualization(skeleton: Skeleton3D) -> void:
	const Visualizer = preload("res://addons/mu_tools/skeleton_visualizer.gd")
	var visualizer = Visualizer.new()
	visualizer.name = "SkeletonVisualizer"
	visualizer.skeleton = skeleton
	visualizer.bone_size = 0.02
	visualizer.line_thickness = 0.005
	skeleton.add_child(visualizer)
	
	print("✓ Added dynamic skeleton visualization")
