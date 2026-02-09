@tool
extends Node3D

class_name MUCharacter

## MUCharacter (Phase 6)
## Manages a master skeleton and multiple skinned mesh parts.

@export_file("*.tscn") var master_skeleton_path: String = "res://assets/converted/players/Player.tscn"

var master_skeleton: Skeleton3D
var animation_player: AnimationPlayer

func _ready():
	# Apply Z-up to Y-up rotation at character level
	rotation = MUCoordinateUtils.get_root_rotation()
	
	if not master_skeleton_path.is_empty():
		load_skeleton(master_skeleton_path)

## Loads the base skeleton from a converted BMD scene
func load_skeleton(path: String):
	if master_skeleton:
		master_skeleton.queue_free()
	
	var scene = load(path).instantiate()
	
	# The converted scene has a root Node3D with rotation
	# We need to extract the skeleton and reset the rotation
	var scene_root = scene
	if scene_root.rotation != Vector3.ZERO:
		scene_root.rotation = Vector3.ZERO  # Remove the baked-in rotation
	
	add_child(scene)
	
	# Find skeleton and anim player
	master_skeleton = _find_node_by_type(scene, "Skeleton3D")
	animation_player = _find_node_by_type(scene, "AnimationPlayer")
	
	if master_skeleton:
		print("[MUCharacter] Master skeleton loaded: ", master_skeleton.get_bone_count(), " bones")
	else:
		push_error("[MUCharacter] Failed to find Skeleton3D in master scene")

## Attaches a mesh part (armor, boots, etc.) to the master skeleton
func add_part(path: String) -> MeshInstance3D:
	if not master_skeleton:
		push_error("[MUCharacter] Cannot add part without a master skeleton")
		return null
		
	var part_scene = load(path).instantiate()
	
	# Reset the part scene's root rotation (it has its own baked-in rotation)
	if part_scene.rotation != Vector3.ZERO:
		part_scene.rotation = Vector3.ZERO
	
	var mesh_node: MeshInstance3D = _find_node_by_type(part_scene, "MeshInstance3D")
	
	if not mesh_node:
		push_error("[MUCharacter] Failed to find MeshInstance3D in part: ", path)
		part_scene.queue_free()
		return null
		
	# Reparent the mesh to our skeleton
	var parent = mesh_node.get_parent()
	if parent: parent.remove_child(mesh_node)
	
	master_skeleton.add_child(mesh_node)
	mesh_node.owner = owner if owner else self
	
	# Remap bone indices if skeleton structures differ
	_remap_bone_indices(mesh_node, part_scene)
	
	mesh_node.skeleton = mesh_node.get_path_to(master_skeleton)
	
	print("[MUCharacter] Added part: ", path)
	part_scene.queue_free()
	return mesh_node

func _remap_bone_indices(mesh_instance: MeshInstance3D, part_source_scene: Node):
	var part_skeleton: Skeleton3D = _find_node_by_type(part_source_scene, "Skeleton3D")
	if not part_skeleton:
		return
		
	# 1. Create a map from part bone index to master bone index
	var index_map = {}
	for i in range(part_skeleton.get_bone_count()):
		var bname = part_skeleton.get_bone_name(i)
		var is_generic = bname.begins_with("Dummy") or bname.begins_with("Bone") or bname.is_empty()
		
		var master_idx = -1
		if not is_generic:
			master_idx = master_skeleton.find_bone(bname)
			if master_idx == -1:
				var normalized_name = bname.strip_edges().replace("Leg", "Thigh").replace("Leg1", "Calf")
				master_idx = master_skeleton.find_bone(normalized_name)
		
		if master_idx == -1 and i < master_skeleton.get_bone_count():
			master_idx = i
			
		index_map[i] = master_idx if master_idx != -1 else 0

	# 2. Create the new Skin
	var new_skin = Skin.new()
	var old_skin = mesh_instance.skin
	if not old_skin: return
		
	for i in range(old_skin.get_bind_count()):
		var part_bone_idx = old_skin.get_bind_bone(i)
		var master_idx = index_map.get(part_bone_idx, 0)
		# Use inverse global rest from master skeleton
		new_skin.add_bind(master_idx, master_skeleton.get_bone_global_rest(master_idx).inverse())
			
	mesh_instance.skin = new_skin
	print("[MUCharacter] Skin remapped for ", mesh_instance.name, " using ", new_skin.get_bind_count(), " binds")

func play_animation(anim_name: String):
	if animation_player:
		animation_player.play(anim_name)
	else:
		print("[MUCharacter] No AnimationPlayer found")

func _find_node_by_type(root: Node, type_name: String) -> Node:
	if root.get_class() == type_name or root.is_class(type_name):
		return root
	for child in root.get_children():
		var res = _find_node_by_type(child, type_name)
		if res: return res
	return null
