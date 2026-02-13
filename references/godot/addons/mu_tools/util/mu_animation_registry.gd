@tool
# class_name MUAnimationRegistry

## Animation Name Registry
## Maps BMD action indices to human-readable names based on source code enums.

enum ModelCategory {
	PLAYER,
	MONSTER,
	ITEM,
	OBJECT,
	UNKNOWN
}

## Player Actions (from _enum.h)
const PLAYER_ACTIONS = {
	0: "Set",
	1: "Stop",
	2: "Stop_Female",
	3: "Stop_Summoner",
	4: "Stop_Sword",
	5: "Stop_Two_Hand_Sword",
	6: "Stop_Spear",
	7: "Stop_Scythe",
	8: "Stop_Bow",
	9: "Stop_Crossbow",
	10: "Stop_Wand",
	11: "Stop_Fly",
	12: "Stop_Fly_Crossbow",
	13: "Stop_Ride",
	14: "Stop_Ride_Weapon",
	15: "Walk",
	16: "Walk_Female",
	17: "Walk_Sword",
	18: "Walk_Two_Hand_Sword",
	19: "Walk_Spear",
	20: "Walk_Scythe",
	21: "Walk_Bow",
	22: "Walk_Crossbow",
	23: "Walk_Wand",
	24: "Walk_Swim",
	25: "Run",
	26: "Run_Sword",
	27: "Run_Two_Sword",
	28: "Run_Two_Hand_Sword",
	29: "Run_Spear",
	30: "Run_Bow",
	31: "Run_Crossbow",
	32: "Run_Wand",
	33: "Run_Swim",
	34: "Fly",
	35: "Fly_Crossbow",
	36: "Run_Ride",
	37: "Run_Ride_Weapon",
}

## Standard Monster Actions (from ZzzOpenData.cpp)
const MONSTER_ACTIONS = {
	0: "Stop1",
	1: "Stop2",
	2: "Walk",
	3: "Attack1",
	4: "Attack2",
	5: "Shock",
	6: "Die",
	7: "Apear",
	8: "Attack3",
	9: "Attack4",
	10: "Attack5",
}

## Metadata categorizing actions that should NOT loop
const NON_LOOPING_ACTIONS = [
	24, # Shock
	25, # Die1
	26, # Die2
	29, # Greeting
	30, # Goodbye
	31, # Clap
	32, # Gesture
	33, # Direction
]

## Weapon-specific Idle (Stop) Actions
const WEAPON_IDLE_MAP = {
	"Sword": 4,         # PLAYER_STOP_SWORD
	"TwoHandSword": 5,  # PLAYER_STOP_TWO_HAND_SWORD
	"Spear": 6,         # PLAYER_STOP_SPEAR
	"Scythe": 7,        # PLAYER_STOP_SCYTHE
	"Bow": 8,           # PLAYER_STOP_BOW
	"Crossbow": 9,      # PLAYER_STOP_CROSSBOW
	"Wand": 10,         # PLAYER_STOP_WAND
	"Staff": 5,         # Staves often use TwoHandSword idle in MU
}

## Guesses the model category based on file path
static func get_category(path: String) -> ModelCategory:
	var lower_path = path.to_lower()
	if "player.bmd" in lower_path:
		return ModelCategory.PLAYER
	if "/monster/" in lower_path:
		return ModelCategory.MONSTER
	if "/player/" in lower_path:
		return ModelCategory.PLAYER
	if "/npc/" in lower_path:
		return ModelCategory.MONSTER # NPCs usually follow monster action patterns
	return ModelCategory.UNKNOWN

## Returns a human-readable name for an action index
static func get_action_name(path: String, index: int) -> String:
	var category = get_category(path)
	
	match category:
		ModelCategory.PLAYER:
			if index in PLAYER_ACTIONS:
				return PLAYER_ACTIONS[index]
		ModelCategory.MONSTER:
			if index in MONSTER_ACTIONS:
				return MONSTER_ACTIONS[index]
	
	return "Action_%d" % index

## Returns whether an action should loop in Godot
static func is_looping(path: String, index: int) -> bool:
	var category = get_category(path)
	if category != ModelCategory.PLAYER:
		# Monsters: usually everything except Shock (5) and Die (6) loops
		return index != 5 and index != 6
	
	return not (index in NON_LOOPING_ACTIONS)

## Gets the appropriate idle action for a weapon class
static func get_idle_action_for_weapon(weapon_class: String) -> int:
	if weapon_class in WEAPON_IDLE_MAP:
		return WEAPON_IDLE_MAP[weapon_class]
	return 1 # Standard Stop (Male)

## PlaySpeed Multipliers (from ZzzOpenData.cpp)
## These are used to scale the 25.0 FPS base rate.
const PLAY_SPEEDS = {
	"PLAYER": {
		0: 0.28,  # Set
		1: 0.28,  # Stop
		2: 0.28,  # Stop_Female
		4: 0.26,  # Stop_Sword
		5: 0.24,  # Stop_Two_Hand_Sword
		6: 0.24,  # Stop_Spear
		8: 0.22,  # Stop_Bow
		9: 0.22,  # Stop_Crossbow
		10: 0.30, # Stop_Wand
		15: 0.30, # Walk
		23: 0.44, # Walk_Wand
		25: 0.45, # Run
		32: 0.76, # Run_Wand
		54: 0.4,  # Shock
	},
	"MONSTER": {
		# Default monster speed is usually around 0.3-0.4
	}
}

## Returns the legacy PlaySpeed multiplier for an action
static func get_play_speed(path: String, index: int) -> float:
	var category = get_category(path)
	var cat_key = "MONSTER"
	if category == ModelCategory.PLAYER:
		cat_key = "PLAYER"
	
	if cat_key in PLAY_SPEEDS and index in PLAY_SPEEDS[cat_key]:
		return PLAY_SPEEDS[cat_key][index]
		
	return 1.0 # Default if unknown
