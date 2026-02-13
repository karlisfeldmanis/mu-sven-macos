@tool
extends Node3D

func _ready():
	print("\n======================================================================")
	print("MINIMAP OVERLAY TEST")
	print("======================================================================")
	
	# 1. Load Minimap Texture
	var map_res = load("res://mini_map_high_res.png")
	if not map_res:
		print("FAILED to load mini_map_high_res.png")
		return
		
	# 2. Create Plane
	var mesh_instance = MeshInstance3D.new()
	var plane_mesh = PlaneMesh.new()
	plane_mesh.size = Vector2(256, 256) # 256x256 MU Tiles
	mesh_instance.mesh = plane_mesh
	
	var mat = StandardMaterial3D.new()
	mat.albedo_texture = map_res
	mat.shading_mode = StandardMaterial3D.SHADING_MODE_UNSHADED
	mesh_instance.material_override = mat
	
	# MU Mapping Alignment:
	# MU(0,0) is bottom-left in MU coordinates.
	# Godot mapping: mu_to_godot(0,0) -> (0, 0, 256)
	# Godot mapping: mu_to_godot(256, 256) -> (256, 0, 0)
	
	# The plane center is (0,0). mu_to_godot(128, 128) -> (128, 0, 128)
	# So we offset the plane to center it around the middle of the world (128, 128)
	mesh_instance.position = Vector3(128, -0.1, 128)
	
	add_child(mesh_instance)
	
	# 3. Add Markers from Minimap.bmd (Lorencia)
	var markers = [
		{"name": "NPC 0", "pos": Vector2(121, 108), "kind": 1},
		{"name": "NPC 4", "pos": Vector2(114, 141), "kind": 1},
		{"name": "City Center", "pos": Vector2(128, 128), "kind": 0},
	]
	
	for m in markers:
		var marker = MeshInstance3D.new()
		var sphere = SphereMesh.new()
		sphere.radius = 1.0
		marker.mesh = sphere
		
		# Use mu_to_godot logic
		var mu_x = m.pos.x
		var mu_y = m.pos.y
		marker.position = Vector3(mu_y, 0, 256 - mu_x)
		
		var m_mat = StandardMaterial3D.new()
		m_mat.albedo_color = Color.RED if m.kind == 1 else Color.GREEN
		marker.material_override = m_mat
		
		add_child(marker)
		print("Added Marker: %s at %v -> %v" % [m.name, m.pos, marker.position])

	print("\nMinimap overlay set up. Inspect in Editor.")
