@tool
# class_name MUStateMachine
extends Node

## Character State Machine
##
## Manages high-level character states (Idle, Walk, Run, Attack)
## and chooses the correct BMD action based on equipment and environment.

enum State { IDLE, WALK, RUN, ATTACK, SHOCK, DIE }

@export var character_path: String = ""
@export var current_state: State = State.IDLE
@export var weapon_class: String = "" # e.g., "Sword", "Bow", "None"
@export var is_safe_zone: bool = false
@export var is_flying: bool = false

var _anim_player: AnimationPlayer
const MUAnimationRegistry = preload("res://addons/mu_tools/util/mu_animation_registry.gd")

func setup(player: AnimationPlayer, path: String) -> void:
	_anim_player = player
	character_path = path

func set_state(new_state: State) -> void:
	current_state = new_state
	update_animation()

func update_animation() -> void:
	if not _anim_player: return
	
	var action_idx: int = 0
	
	match current_state:
		State.IDLE:
			action_idx = _get_idle_action()
		State.WALK:
			action_idx = 1 # Stop_Walk
		State.RUN:
			action_idx = 2 # Stop_Run
		State.DIE:
			action_idx = 25 # Die1
			
	var anim_name = MUAnimationRegistry.get_action_name(character_path, action_idx)
	if _anim_player.has_animation(anim_name):
		_anim_player.play(anim_name)

func _get_idle_action() -> int:
	# Mirroring SVEN's SetPlayerStop logic (simplified)
	if is_safe_zone:
		return 1 # Standard Stop (Male)
		
	if weapon_class != "" and weapon_class != "None":
		return MUAnimationRegistry.get_idle_action_for_weapon(weapon_class)
		
	return 1 # Standard Stop (Male)
