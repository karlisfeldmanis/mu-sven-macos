extends Node
class_name LightFlicker

@export var min_energy: float = 0.5
@export var max_energy: float = 1.2
@export var flicker_speed: float = 10.0

var base_energy: float = 1.0
var time: float = 0.0

func _ready():
	if get_parent() is Light3D:
		base_energy = get_parent().light_energy

func _process(delta):
	var light = get_parent()
	if not light is Light3D: return
	
	time += delta * flicker_speed
	var noise = randf_range(-0.1, 0.1) # Simple jitter
	var sine = sin(time) * 0.2
	
	light.light_energy = base_energy * (1.0 + sine + noise)
	light.light_energy = clamp(light.light_energy, min_energy, max_energy)
