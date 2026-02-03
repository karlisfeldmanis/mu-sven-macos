extends Node3D

## Visual Rendering Test
## Loads BMD with skeleton + meshes for visual inspection

@export_file("*.bmd") var bmd_file_path: String = "res://raw_data/Player/ArmorClass01.bmd"

var _anim_player: AnimationPlayer
var _current_action: int = 0
var _state_machine: MUStateMachine

const MUAnimationRegistry = preload("res://addons/mu_tools/mu_animation_registry.gd")
const MUStateMachine = preload("res://addons/mu_tools/mu_state_machine.gd")

func _ready() -> void:
	load_character_with_state_machine(bmd_file_path)

func _on_speed_changed(val: float) -> void:
	if _anim_player:
		_anim_player.speed_scale = val

func _process(_delta: float) -> void:
	pass

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
	
	# Setup State Machine
	_state_machine = MUStateMachine.new()
	add_child(_state_machine)
	_state_machine.setup(_anim_player, base_path)
	_state_machine.weapon_class = "None" # Start clean
	_state_machine.set_state(MUStateMachine.State.IDLE)
	_state_machine.update_animation()
	
	print("\n=== Character loaded and idling! ===")


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
	var character = Node3D.new()
	character.name = "Character"
	character.rotation.x = -PI / 2.0
	add_child(character)
	
	skeleton.name = "Skeleton"
	character.add_child(skeleton)
	
	# 1. Setup AnimationPlayer
	_anim_player = AnimationPlayer.new()
	_anim_player.name = "AnimationPlayer"
	character.add_child(_anim_player)
	
	# 2. Build Animations
	const AnimationBuilder = preload("res://addons/mu_tools/animation_builder.gd")
	var library = AnimationBuilder.build_animation_library(path, parser.actions, skeleton)
	_anim_player.add_animation_library("", library)
	
	# 3. Default Idle Loop (Action 1 = Stop_Male)
	var idle_name = MUAnimationRegistry.get_action_name(path, 1)
	if _anim_player.has_animation(idle_name):
		_anim_player.play(idle_name)
		_current_action = 1
	
	return skeleton

func attach_bmd_meshes(path: String, skeleton: Skeleton3D) -> void:
	var parser = BMDParser.new()
	if not parser.parse_file(path, false): # debug=false
		return
		
	print("Attaching meshes from: ", path.get_file())
	
	for i in range(parser.meshes.size()):
		var bmd_mesh = parser.meshes[i]
		print("  [Mesh %d] Vertices: %d" % [i, bmd_mesh.vertex_count])
		
		var mesh = MUMeshBuilder.build_mesh(bmd_mesh, skeleton, false)
		
		if mesh:
			var mesh_instance = MeshInstance3D.new()
			mesh_instance.mesh = mesh
			mesh_instance.name = path.get_file().get_basename() + "_%d" % i
			
			var material = StandardMaterial3D.new()
			material.albedo_color = Color(0.8, 0.8, 0.8)
			material.metallic = 0.2
			material.roughness = 0.5
			material.cull_mode = BaseMaterial3D.CULL_DISABLED
			mesh_instance.set_surface_override_material(0, material)
			
			skeleton.add_child(mesh_instance)
			mesh_instance.skeleton = mesh_instance.get_path_to(skeleton)
			
			var skin = Skin.new()
			skin.set_bind_count(skeleton.get_bone_count())
			for j in range(skeleton.get_bone_count()):
				skin.set_bind_bone(j, j)
				skin.set_bind_pose(j, Transform3D.IDENTITY)
			mesh_instance.skin = skin

func _add_skeleton_visualization(skeleton: Skeleton3D) -> void:
	const Visualizer = preload("res://addons/mu_tools/skeleton_visualizer.gd")
	var visualizer = Visualizer.new()
	visualizer.name = "SkeletonVisualizer"
	visualizer.skeleton = skeleton
	visualizer.bone_size = 0.02
	visualizer.line_thickness = 0.005
	skeleton.add_child(visualizer)
	
	print("✓ Added dynamic skeleton visualization")
