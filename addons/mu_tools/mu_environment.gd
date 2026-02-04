extends Node

## MUEnvironment
## Global controller for environmental systems (Wind, Water, Time)

class_name MUEnvironment

# Shared oscillation parameters (from Sven source)
var world_time: float = 0.0

func _process(delta: float):
	world_time += delta * 1000.0 # Convert to ms to match Sven logic
	
	# Sven Logic (ZzzLodTerrain.cpp:2386)
	# WindSpeed = (int)WorldTime % (360000 * 2) * (0.002f);
	# WindScale = 10.f; (for EnableEvent == 0, Lorencia)
	
	var time_int = int(world_time)
	var wind_speed = float(time_int % 720000) * 0.002
	var wind_scale = 10.0
	
	# Water Move Logic
	# WaterMove = (float)((int)(WorldTime) % 40000) * 0.000025f;
	var water_move = float(time_int % 40000) * 0.000025
	
	# Update Global Shader Parameters
	RenderingServer.global_shader_parameter_set("mu_world_time", world_time)
	RenderingServer.global_shader_parameter_set("mu_wind_speed", wind_speed)
	RenderingServer.global_shader_parameter_set("mu_wind_scale", wind_scale)
	RenderingServer.global_shader_parameter_set("mu_water_move_uv", water_move)
	RenderingServer.global_shader_parameter_set("mu_wind_direction", 
			Vector3(1.0, 0.0, 0.5).normalized())
