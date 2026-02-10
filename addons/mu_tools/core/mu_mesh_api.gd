extends Node
# MUMeshAPI.gd
# Interface for terrain and BMD mesh generation.

const MUTerrainMeshBuilder = preload("res://addons/mu_tools/nodes/mu_terrain_mesh_builder.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/nodes/mesh_builder.gd")
const MUBMDRegistry = preload("res://addons/mu_tools/util/mu_bmd_registry.gd")
const MUObjLoader = preload("res://addons/mu_tools/core/mu_obj_loader.gd")

## Build a terrain mesh from height and light data
func build_terrain_mesh(heightmap: PackedFloat32Array, lightmap: Image) -> ArrayMesh:
	var builder = MUTerrainMeshBuilder.new()
	return builder.build_terrain_array_mesh(heightmap, lightmap)

## Load and construct a static BMD mesh
func build_bmd_mesh_instance(bmd_path: String, mesh_idx: int = 0) -> MeshInstance3D:
	var parser = MUBMDRegistry.get_bmd(bmd_path)
	if not parser: return null
	
	var bmd_mesh = parser.get_mesh(mesh_idx)
	return MUMeshBuilder.create_mesh_instance(bmd_mesh, null, bmd_path, parser, true)

## Build all meshes for a complex BMD object as a Node3D hierarchy
func build_complex_bmd_node(bmd_path: String) -> Node3D:
	var parser = MUBMDRegistry.get_bmd(bmd_path)
	if not parser: return null
	
	var root = Node3D.new()
	for i in range(parser.get_mesh_count()):
		var mi = build_bmd_mesh_instance(bmd_path, i)
		if mi: root.add_child(mi)
	return root

## Load an OBJ file as a MeshInstance3D with auto-resolved materials
func build_obj_mesh_instance(obj_path: String) -> MeshInstance3D:
	return MUObjLoader.build_mesh_instance(obj_path)
