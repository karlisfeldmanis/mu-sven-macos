extends Node3D

func _ready():
	print("--- MU Terrain Test ---")
	
	# Create environment
	var env = WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.1, 0.1, 0.15)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color.WHITE
	env.environment.ambient_light_energy = 0.5
	add_child(env)
	
	var sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 45, 0)
	add_child(sun)
	
	# Instantiate Terrain
	var mut_script = load("res://addons/mu_tools/nodes/mu_terrain.gd")
	var terrain = mut_script.new()
	terrain.world_id = 1 # Lorencia (World1)
	# Use the relative path for the reference assets
	terrain.data_path = "res://reference/MuMain/src/bin/Data/World1"
	add_child(terrain)
	terrain.load_world()
	
	# Setup Camera (MU Angles)
	var cam_base = Node3D.new()
	add_child(cam_base)
	cam_base.position = Vector3(128, 0, 128) # Center of 256x256 map
	
	var cam_yaw = Node3D.new()
	cam_base.add_child(cam_yaw)
	cam_yaw.rotation_degrees.y = -45
	
	var cam_pitch = Node3D.new()
	cam_yaw.add_child(cam_pitch)
	cam_pitch.rotation_degrees.x = -48.5
	
	var camera = Camera3D.new()
	cam_pitch.add_child(camera)
	camera.position.z = 35 # Zoom out a bit to see the map
	camera.fov = 30.0
	camera.make_current()
	
	print("--- Terrain loaded! ---")
	get_tree().quit()
