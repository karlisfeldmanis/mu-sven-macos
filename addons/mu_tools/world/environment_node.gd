extends Node

## MUEnvironment
## Global controller for environmental systems (Wind, Water, Time)

# class_name MUEnvironment

# Shared oscillation parameters (from Sven source)
var world_time: float = 0.0
var _env_node: WorldEnvironment
var _sky_mat: ShaderMaterial

func _process(delta: float):
	world_time += delta * 1000.0 # Convert to ms to match Sven logic
	
	# Sven Logic (ZzzLodTerrain.cpp:2386)
	# WindSpeed = (int)WorldTime % (360000 * 2) * (0.002f);
	# WindScale = 10.f; (for EnableEvent == 0, Lorencia)
	
	var time_int = int(world_time)
	var wind_speed = float(time_int % 720000) * 0.002
	var wind_scale = 10.0
	
	# Water Move Logic (SVEN Parity)
	# WaterMove = (float)((int)(WorldTime) % 20000) * 0.00005f;
	var water_move = float(time_int % 20000) * 0.00005
	
	# Update Global Shader Parameters
	RenderingServer.global_shader_parameter_set("mu_world_time", world_time)
	RenderingServer.global_shader_parameter_set("mu_wind_speed", wind_speed)
	RenderingServer.global_shader_parameter_set("mu_wind_scale", wind_scale)
	RenderingServer.global_shader_parameter_set("mu_water_move_uv", water_move)
	RenderingServer.global_shader_parameter_set("mu_wind_direction", 
			Vector3(1.0, 0.0, 0.5).normalized())

func setup_environment(world_id: int):
	# 1. Create WorldEnvironment
	_env_node = WorldEnvironment.new()
	add_child(_env_node)
	
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	
	# Atmospheric Fog (SVEN Parity: 2000-2700 range, but fading to BLACK)
	env.fog_enabled = true
	env.fog_light_color = Color.BLACK
	env.fog_density = 0.0 # Linear fog using depth
	env.fog_aerial_perspective = 0.0
	env.fog_sky_affect = 1.0 # Fully affect the black background
	env.fog_depth_begin = 50.0  # SVEN: 2000.f / SCALE
	env.fog_depth_end = 250.0   # SVEN: 2700.f / SCALE
	
	_env_node.environment = env

	if world_id == 0:
		print("[MUEnvironment] Setup Lorencia (Standard Wind + Reactive Sky)")

func set_lightmap(tex: Texture2D):
	if _sky_mat:
		_sky_mat.set_shader_parameter("lightmap_tex", tex)


## Disable fog (useful for raw model viewing where scale is huge)
func disable_fog():
	if _env_node and _env_node.environment:
		_env_node.environment.fog_enabled = false
