extends Node3D

## Automated Forward+ Renderer Test
## Opens a window, renders the world, saves screenshots, then quits
## Usage: godot --script scripts/run_forward_plus_test.gd -- World1 res://reference/MuMain/Data

var _world_id: int = 1
var _data_path: String = "res://reference/MuMain/src/bin/Data"
var _terrain: Node

func _ready() -> void:
	print("=== Forward+ Automated Test ===")
	print("Rendering Driver: ", RenderingServer.get_video_adapter_name())
	print()
	
	# Parse command line args
	var args = OS.get_cmdline_args()
	for i in range(args.size()):
		if args[i] == "--":
			if i + 1 < args.size():
				var world_arg = args[i + 1]
				if world_arg.begins_with("World"):
					_world_id = int(world_arg.trim_prefix("World"))
			if i + 2 < args.size():
				_data_path = args[i + 2]
			break
	
	print("World ID: ", _world_id)
	print("Data Path: ", _data_path)
	print()
	
	# Setup scene
	_setup_camera()
	_setup_lighting()
	_setup_environment()
	_load_world()
	
	# Wait for rendering then capture and quit
	await get_tree().create_timer(3.0).timeout
	_capture_screenshots()
	await get_tree().create_timer(0.5).timeout
	
	print("✓ Test complete!")
	get_tree().quit()

func _setup_camera() -> void:
	var camera = Camera3D.new()
	camera.name = "MainCamera"
	camera.current = true
	camera.position = Vector3(128, 80, -100)
	camera.look_at(Vector3(128, 0, 128))
	camera.fov = 60.0
	add_child(camera)
	print("✓ Camera setup complete")

func _setup_lighting() -> void:
	# Directional light (sun)
	var sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.transform.basis = Basis.looking_at(Vector3(-1, -0.5, -1))
	sun.light_color = Color(1.0, 0.98, 0.95)  # Warm white
	sun.light_energy = 1.2
	sun.shadow_enabled = true
	sun.shadow_bias = 0.1
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.directional_shadow_max_distance = 500.0
	add_child(sun)
	
	# Ambient light via environment is set up separately
	print("✓ Lighting setup complete (shadows enabled)")

func _setup_environment() -> void:
	var world_env = WorldEnvironment.new()
	var env = Environment.new()
	
	# Sky
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.5, 0.6)  # Blue sky
	
	# Ambient light
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.7)
	env.ambient_light_energy = 0.4
	
	# Fog (matching SVEN)
	env.fog_enabled = true
	env.fog_mode = Environment.FOG_MODE_DEPTH
	env.fog_light_color = Color(30.0/255, 20.0/255, 10.0/255)  # Brownish
	env.fog_depth_begin = 300.0
	env.fog_depth_end = 800.0
	
	# Tone mapping for better colors
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	
	world_env.environment = env
	add_child(world_env)
	print("✓ Environment setup complete (fog enabled)")

func _load_world() -> void:
	print("Loading World %d..." % _world_id)
	
	var mut_script = load("res://addons/mu_tools/mu_terrain.gd")
	if not mut_script:
		push_error("Failed to load mu_terrain.gd")
		return
	
	_terrain = mut_script.new()
	_terrain.world_id = _world_id
	_terrain.data_path = _data_path
	add_child(_terrain)
	_terrain.load_world()
	
	print("✓ World loaded")

func _capture_screenshots() -> void:
	print("Capturing screenshots...")
	
	# Ensure rendering is complete
	await get_tree().process_frame
	await get_tree().process_frame
	
	var viewport = get_viewport()
	var texture = viewport.get_texture()
	if not texture:
		push_error("Viewport texture is null")
		return
	
	var img = texture.get_image()
	if not img:
		push_error("Viewport image is null")
		return
	
	var timestamp = Time.get_unix_time_from_system()
	var filename = "forward_plus_world%d_%d.png" % [_world_id, int(timestamp)]
	
	var err = img.save_png(filename)
	if err == OK:
		print("✓ Screenshot saved: ", filename)
	else:
		push_error("Failed to save screenshot: ", err)
