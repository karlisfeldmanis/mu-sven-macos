extends Node3D

const MUCoordinates = preload("res://addons/mu_tools/core/mu_coordinate_utils.gd")

func _ready():
	print("=== Fence/World Object Orientation Test ===")
	
	var data_dir = "res://reference/MuMain/src/bin/Data/Object1/"
	var spacing = 5.0
	var objects = ["Fence01.bmd", "Tree01.bmd", "Sign01.bmd", "Stone01.bmd"]
	
	for j in range(objects.size()):
		var bmd_name = objects[j]
		var bmd_path = data_dir.path_join(bmd_name)
		var abs_path = ProjectSettings.globalize_path(bmd_path)
		
		if not FileAccess.file_exists(abs_path):
			continue
			
		var parser = BMDParser.new()
		if not parser.parse_file(abs_path): 
			continue
			
		# This container uses the IDENTICAL logic from mu_terrain.gd
		var parent_node = Node3D.new()
		parent_node.name = bmd_name.replace(".bmd", "_Placement")
		parent_node.position.x = j * spacing
		add_child(parent_node)
		
		var mu_container = Node3D.new()
		mu_container.name = "MUContainer"
		parent_node.add_child(mu_container)
		
		# MIRROR OF mu_terrain.gd lines 554-559
		# Assumes obj.rotation is (0,0,0) for these simple objects
		var raw_mu_rotation = Vector3(0, 0, 0) 
		mu_container.rotation_order = EULER_ORDER_XZY
		mu_container.rotation = MUCoordinates.mu_angle_to_godot_rotation(raw_mu_rotation)
		
		print("\n--- Testing ", bmd_name, " ---")
		print("  MUContainer rotation (Godot): ", mu_container.rotation)
		
		for i in range(parser.get_mesh_count()):
			var bmd_mesh = parser.get_mesh(i)
			var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, null, abs_path)
			if mesh_instance:
				mu_container.add_child(mesh_instance)

	# Setup camera 
	var cam = Camera3D.new()
	cam.position = Vector3(spacing * 2, 5, 10)
	cam.look_at(Vector3(spacing * 2, 0, 0))
	add_child(cam)
	
	var light = DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, 45, 0)
	add_child(light)
	
	var env = WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color.DARK_GRAY
	add_child(env)

	print("\n=== Mirror Test Ready. Are they horizontal? ===")
