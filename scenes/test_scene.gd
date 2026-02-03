extends Node3D

## Test Scene for BMD Model Loading
##
## Usage:
## 1. Place a .bmd file in raw_data/Player/
## 2. Set the bmd_file_path variable below
## 3. Run the scene

@export_file("*.bmd") var bmd_file_path: String = "res://raw_data/Player/Player.bmd"
@export var debug_output: bool = true

func _ready() -> void:
	if bmd_file_path.is_empty():
		print("[Test Scene] No BMD file specified")
		return
	
	if not FileAccess.file_exists(bmd_file_path):
		print("[Test Scene] BMD file not found: ", bmd_file_path)
		return
	
	load_bmd_model(bmd_file_path)

func load_bmd_model(path: String) -> void:
	print("[Test Scene] Loading BMD: ", path)
	
	# Parse BMD file
	var parser = BMDParser.new()
	if not parser.parse_file(path, debug_output):
		push_error("[Test Scene] Failed to parse BMD file")
		return
	
	# Get character root node
	var character_root = $CharacterRoot
	
	# Build and add meshes
	for i in range(parser.get_mesh_count()):
		var bmd_mesh = parser.get_mesh(i)
		var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh)
		
		if mesh_instance:
			mesh_instance.name = "Mesh_" + str(i)
			character_root.add_child(mesh_instance)
			
			if debug_output:
				print("[Test Scene] Added mesh: ", mesh_instance.name)
	
	print("[Test Scene] Model loaded successfully!")
	print("  Total meshes: ", parser.get_mesh_count())
	print("  Total bones: ", parser.get_bone_count())
