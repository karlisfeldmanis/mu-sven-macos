extends Node3D

func _ready():
	print("=== Comprehensive Tree Test ===")
	
	var data_dir = "res://reference/MuMain/src/bin/Data/Object1/"
	var spacing = 6.0
	var columns = 4
	
	# Find all TreeXX.bmd files
	var trees = []
	for i in range(1, 20): # Check up to 19 just in case
		var bmd_name = "Tree%02d.bmd" % i
		if FileAccess.file_exists(data_dir.path_join(bmd_name)):
			trees.append(bmd_name)
	
	print("Detected ", trees.size(), " tree models.")
	
	for j in range(trees.size()):
		var bmd_name = trees[j]
		var bmd_path = data_dir.path_join(bmd_name)
		var abs_path = ProjectSettings.globalize_path(bmd_path)
		
		var parser = BMDParser.new()
		if not parser.parse_file(abs_path): 
			printerr("Failed to parse: ", bmd_name)
			continue
			
		var col = j % columns
		var row = j / columns
		
		var tree_node = Node3D.new()
		tree_node.name = bmd_name.replace(".bmd", "")
		tree_node.position = Vector3(col * spacing, 0, -row * spacing)
		add_child(tree_node)
		
		for i in range(parser.get_mesh_count()):
			var bmd_mesh = parser.get_mesh(i)
			var mesh_instance = MUMeshBuilder.create_mesh_instance(bmd_mesh, null, abs_path)
			if mesh_instance:
				tree_node.add_child(mesh_instance)
				
				# Verify material settings
				var mesh = mesh_instance.mesh
				var mat = mesh.surface_get_material(0)
				if mat and mat is ShaderMaterial:
					var scissor = mat.get_shader_parameter("use_alpha_scissor")
					if scissor:
						print("  [", bmd_name, "] Mesh ", i, " using Alpha Scissor (Foliage)")

	# Setup camera 
	var cam = Camera3D.new()
	cam.position = Vector3(columns * spacing / 2.0, 15, 10)
	cam.look_at(Vector3(columns * spacing / 2.0, 2, -spacing))
	add_child(cam)
	
	var light = DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, 45, 0)
	add_child(light)
	
	var env = WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color.DARK_GRAY
	add_child(env)

	print("=== All trees loaded. Check the viewport! ===")
