extends Node3D

## Main Game Scene (Standalone App)
##
## This is the entry point for the standalone Godot application
## Runs without the editor, loads assets at runtime

@export_group("Asset Paths")
@export var character_mesh_dir: String = "res://assets/players/meshes"
@export var character_texture_dir: String = "res://assets/players/textures"

@export_group("Runtime Settings")
@export var auto_load_character: bool = true
@export var default_character: String = "Player"

var loaded_character: Node3D = null

func _ready() -> void:
	print("[MU Remaster] Starting standalone application...")
	print("  Godot Version: ", Engine.get_version_info().string)
	print("  Platform: ", OS.get_name())
	
	# Setup camera and lighting (already in scene)
	setup_environment()
	
	# Load character if auto-load is enabled
	if auto_load_character:
		load_character(default_character)
	
	print("[MU Remaster] Ready!")

func setup_environment() -> void:
	# Additional environment setup if needed
	RenderingServer.set_default_clear_color(Color(0.1, 0.1, 0.15))

func load_character(character_name: String) -> void:
	print("[Character Loader] Loading character: ", character_name)
	
	# Clear existing character
	if loaded_character:
		loaded_character.queue_free()
		loaded_character = null
	
	# Create character root
	var character_root = Node3D.new()
	character_root.name = character_name
	add_child(character_root)
	
	# Load meshes
	var mesh_count = 0
	var mesh_dir = DirAccess.open(character_mesh_dir)
	
	if mesh_dir:
		mesh_dir.list_dir_begin()
		var file_name = mesh_dir.get_next()
		
		while file_name != "":
			if file_name.ends_with(".res") and character_name.to_lower() in file_name.to_lower():
				var mesh_path = character_mesh_dir.path_join(file_name)
				var mesh = load(mesh_path)
				
				if mesh is ArrayMesh:
					var mesh_instance = MeshInstance3D.new()
					mesh_instance.mesh = mesh
					mesh_instance.name = file_name.get_basename()
					character_root.add_child(mesh_instance)
					mesh_count += 1
					print("  ✓ Loaded mesh: ", file_name)
			
			file_name = mesh_dir.get_next()
		
		mesh_dir.list_dir_end()
	
	if mesh_count > 0:
		loaded_character = character_root
		print("[Character Loader] Successfully loaded ", mesh_count, " meshes")
	else:
		# FALLBACK: Create a dummy cube if no meshes found so the user sees SOMETHING
		var dummy = MeshInstance3D.new()
		dummy.mesh = BoxMesh.new()
		var mat = StandardMaterial3D.new()
		mat.albedo_color = Color.RED
		dummy.mesh.material = mat
		character_root.add_child(dummy)
		loaded_character = character_root
		
		print("[Character Loader] WARNING: No meshes found. Displaying dummy cube.")

func _input(event: InputEvent) -> void:
	# Simple controls for testing
	if event is InputEventKey and event.pressed:
		match event.keycode:
			KEY_R:
				# Reload character
				if loaded_character:
					load_character(default_character)
			KEY_ESCAPE:
				# Quit application
				get_tree().quit()

func _process(delta: float) -> void:
	# Rotate character for demonstration
	if loaded_character:
		loaded_character.rotate_y(delta * 0.5)
