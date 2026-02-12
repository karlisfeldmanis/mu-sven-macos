@tool
extends Node3D

## GrassSystem
## Handles all grass rendering (MultiMesh BMDs and Procedural Billboards).

# class_name MUGrassSystem

const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd")
const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")
const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")
const MUMeshBuilder = preload("res://addons/mu_tools/rendering/bmd_mesh_builder.gd")
const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")

func spawn_grass(heightmap: Node3D):
	if not heightmap: return
	
	var objects = heightmap.get_objects_data()
	var world_id = heightmap.world_id
	
	_spawn_multimesh_grass(objects, world_id, heightmap)
	# _spawn_procedural_grass(heightmap) # Future: Migrate from mu_terrain.gd if needed

func _spawn_multimesh_grass(objects: Array, world_id: int, heightmap: Node3D):
	const GRASS_MIN = 20
	const GRASS_MAX = 27
	
	var buckets = {} # bmd_path -> Array[instances]
	var city_filter = Rect2(95, 95, 75, 75) # Standardized (G_X=MU_Y, G_Z=MU_X)
	
	for obj in objects:
		if obj.type < GRASS_MIN or obj.type > GRASS_MAX:
			continue
		
		# Spatial Filter check (Removed to allow global grass)
		# if not city_filter.has_point(Vector2(obj.position.x, obj.position.z)):
		# 	continue
			
		var path = MUModelRegistry.get_object_path(obj.type, world_id)
		if path == "": continue
		
		if not buckets.has(path):
			buckets[path] = []
		buckets[path].append(obj)
		
	var heights = heightmap.get_height_data()
	
	for bmd_path in buckets:
		var insts = buckets[bmd_path]
		var parser = BMDParser.new()
		if not parser.parse_file(ProjectSettings.globalize_path(bmd_path)):
			continue
			
		var mm = MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.instance_count = insts.size()
		
		var mmi = MultiMeshInstance3D.new()
		mmi.multimesh = mm
		mmi.name = "Grass_" + bmd_path.get_file().get_basename()
		add_child(mmi)
		
		# Build source mesh
		var temp_mi = MUMeshBuilder.create_mesh_instance(
			parser.meshes[0], null, bmd_path, parser, true
		)
		var source_mesh = temp_mi.mesh
		var source_mat = source_mesh.surface_get_material(0)
		temp_mi.queue_free()
		
		mm.mesh = source_mesh
		
		# Apply Alpha Scissor Material Override
		var grass_mat = source_mat.duplicate()
		grass_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		grass_mat.alpha_scissor_threshold = 0.5
		grass_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
		mmi.material_override = grass_mat
		
		for i in range(insts.size()):
			var inst = insts[i]
			var t = Transform3D()
			var sc_vec = MUCoordinateUtils.convert_scale(inst.scale)
			t = t.scaled(sc_vec)
			t.basis = Basis(inst.rotation) * t.basis
			
			# Height snapping
			var final_pos = inst.position
			if not heights.is_empty():
				var ground_h = heightmap.get_height_at_world(final_pos)
				final_pos.y = ground_h
				
			t.origin = final_pos
			mm.set_instance_transform(i, t)
		
		print("  [GrassSystem] Instanced %d x %s" % [insts.size(), bmd_path.get_file()])
