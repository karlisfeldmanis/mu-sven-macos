extends Node3D

## Skeleton-Only Test Scene
## Loads BMD skeleton and visualizes it to verify humanoid structure

const MUSkeletonVisualizer = preload("res://addons/mu_tools/ui/skeleton_visualizer.gd")

@export_file("*.bmd") var bmd_file_path: String = "res://raw_data/Player/Player.bmd"
@export var debug_output: bool = true

var visualizer: MUSkeletonVisualizer

func _ready() -> void:
	if bmd_file_path.is_empty():
		print("[Skeleton Test] No BMD file specified")
		return
	
	if not FileAccess.file_exists(bmd_file_path):
		print("[Skeleton Test] BMD file not found: ", bmd_file_path)
		return
	
	load_skeleton_only(bmd_file_path)

func load_skeleton_only(path: String) -> void:
	print("[Skeleton Test] Loading BMD skeleton: ", path)
	
	# Parse BMD file
	var parser = BMDParser.new()
	if not parser.parse_file(path, debug_output):
		push_error("[Skeleton Test] Failed to parse BMD file")
		return
	
	# Build skeleton only (no meshes)
	var skeleton = MUSkeletonBuilder.build_skeleton(parser.bones, parser.actions)
	if not skeleton:
		push_error("[Skeleton Test] Failed to build skeleton")
		return
	
	skeleton.name = "PlayerSkeleton"
	add_child(skeleton)
	
	# Create and attach visualizer
	visualizer = MUSkeletonVisualizer.new()
	visualizer.skeleton = skeleton
	visualizer.show_bone_names = true
	visualizer.bone_size = 0.02
	add_child(visualizer)
	
	# Print skeleton info
	visualizer.print_skeleton_info()
	
	print("\n[Skeleton Test] ✓ Skeleton loaded successfully!")
	print("  Total bones: ", skeleton.get_bone_count())
	print("  Visualizer: Green spheres = bones, Cyan lines = connections")
	print("  Axes: Red=X, Green=Y, Blue=Z")
	
	# List root bones
	var root_bones = []
	for i in range(skeleton.get_bone_count()):
		if skeleton.get_bone_parent(i) == -1:
			root_bones.append(skeleton.get_bone_name(i))
	
	print("\n  Root bones: ", root_bones)
