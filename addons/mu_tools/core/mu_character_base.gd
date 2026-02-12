extends Node3D
# class_name MUCharacterBase

## Modular Equipment System (Phase 3)
## Handles sharing a single Skeleton3D across multiple MeshInstance3Ds.

@export var skeleton_path: NodePath
var master_skeleton: Skeleton3D

func _ready():
	if not skeleton_path.is_empty():
		master_skeleton = get_node(skeleton_path) as Skeleton3D
	
	if master_skeleton:
		sync_meshes()

## Syncs all child MeshInstance3Ds to the master skeleton
func sync_meshes():
	for child in get_children():
		if child is MeshInstance3D:
			_attach_to_skeleton(child)

func _attach_to_skeleton(mesh_instance: MeshInstance3D):
	if master_skeleton:
		# In Godot 4, we set the skeleton path on the MeshInstance3D
		mesh_instance.skeleton = mesh_instance.get_path_to(master_skeleton)
		print("[MU Character] Attached ", mesh_instance.name, " to skeleton")

## Adds a new equipment part (Armor, Helm, etc.)
func add_equipment(mesh_instance: MeshInstance3D):
	add_child(mesh_instance)
	_attach_to_skeleton(mesh_instance)
