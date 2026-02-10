@tool
extends SceneTree

const MUObjLoader = preload("res://addons/mu_tools/core/mu_obj_loader.gd")

func _init():
	var args = OS.get_cmdline_user_args()
	var models_dir = "res://assets/models"
	if args.size() > 0:
		models_dir = args[0]
		
	print("=" .repeat(60))
	print("[Audit] Scanning OBJ geometry health in: ", models_dir)
	print("=" .repeat(60))
	
	var files = _scan_dir(models_dir)
	files.sort()
	
	var total = 0
	var healthy = 0
	var crushed = 0
	var empty = 0
	var exploded = 0

	for f in files:
		total += 1
		var data = MUObjLoader.load_obj(f)
		if data.is_empty() or data["mesh"] == null:
			print("  [Audit] ✗ FAILED: ", f.get_file())
			empty += 1
			continue
			
		var mesh = data["mesh"]
		var aabb = mesh.get_aabb()
		
		# CRITERIA:
		# Crushed: One dimension is near-zero while others are large (skewed/collapsed)
		# Exploded: Any dimension > 500 meters (MU world is ~256m)
		# Empty: No surfaces or no vertices
		
		var sz = aabb.size
		if sz.length() < 0.001:
			print("  [Audit] ⚠ EMPTY: ", f.get_file())
			empty += 1
		elif sz.x > 500 or sz.y > 500 or sz.z > 500:
			print("  [Audit] ✗ EXPLODED: ", f.get_file(), " Size: ", sz)
			exploded += 1
		elif (sz.x < 0.01 and (sz.y > 0.1 or sz.z > 0.1)) or \
			 (sz.y < 0.01 and (sz.x > 0.1 or sz.z > 0.1)) or \
			 (sz.z < 0.01 and (sz.x > 0.1 or sz.y > 0.1)):
			# Note: Many flat textures (leaves/signs) might trigger this. 
			# We narrow it down to things with many vertices that are crushed.
			var v_count = 0
			for s in range(mesh.get_surface_count()):
				v_count += mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX].size()
			if v_count > 100:
				print("  [Audit] ⚠ CRUSHED: ", f.get_file(), " Size: ", sz, " Verts: ", v_count)
				crushed += 1
			else:
				healthy += 1
		else:
			healthy += 1

	print("-" .repeat(60))
	print("[Audit] Total: %d | Healthy: %d | Crushed: %d | Exploded: %s | Empty: %d" % \
		[total, healthy, crushed, exploded, empty])
	
	quit()

func _scan_dir(path: String) -> Array:
	var out = []
	var dir = DirAccess.open(path)
	if not dir: return []
	dir.list_dir_begin()
	var name = dir.get_next()
	while name != "":
		if not name.begins_with(".") and name.ends_with(".obj"):
			out.append(path.path_join(name))
		name = dir.get_next()
	return out
