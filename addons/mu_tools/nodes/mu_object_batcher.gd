@tool
extends Node3D

# class_name MUObjectBatcher

## Manages MultiMeshInstance3D for repetitive map objects (walls, fences, etc.)

const MUObjLoaderClass = preload("res://addons/mu_tools/parsers/obj_loader.gd")

# Internal map: mesh_path -> { mesh: Mesh, transforms: Array[Transform3D], materials: Array[Material] }
var _batches: Dictionary = {}

func clear():
	for child in get_children():
		child.queue_free()
	_batches.clear()

func add_object(path: String, transform: Transform3D):
	if not _batches.has(path):
		_batches[path] = {
			"transforms": []
		}
	
	_batches[path].transforms.append(transform)

func finalize():
	for path in _batches:
		var data = _batches[path]
		var transforms = data.transforms
		
		# Load the model once to get the mesh
		var mesh: Mesh = _load_mesh_for_path(path)
		if not mesh:
			continue
			
		var multimesh = MultiMesh.new()
		multimesh.transform_format = MultiMesh.TRANSFORM_3D
		multimesh.instance_count = transforms.size()
		multimesh.mesh = mesh
		
		for i in range(transforms.size()):
			multimesh.set_instance_transform(i, transforms[i])
			
		var mmi = MultiMeshInstance3D.new()
		mmi.name = path.get_file().get_basename() + "_Batch"
		mmi.multimesh = multimesh
		add_child(mmi)

func _load_mesh_for_path(path: String) -> Mesh:
	if path.ends_with(".tscn"):
		var scene = load(path)
		if scene is PackedScene:
			var inst = scene.instantiate()
			var mesh = _find_mesh_recursive(inst)
			inst.free()
			return mesh
	elif path.ends_with(".obj"):
		var data = MUObjLoaderClass.load_obj(path)
		if not data.is_empty():
			return data.mesh
	return null

func _find_mesh_recursive(node: Node) -> Mesh:
	if node is MeshInstance3D:
		return node.mesh
	for child in node.get_children():
		var m = _find_mesh_recursive(child)
		if m: return m
	return null

func _find_first_mesh(node: Node) -> Mesh:
	if node is MeshInstance3D:
		return node.mesh
	for child in node.get_children():
		var m = _find_first_mesh(child)
		if m: return m
	return null
