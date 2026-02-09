@tool
extends Node

class_name MUModelRegistry

## Centralized registry for MU Online model metadata.
## Resolves heuristics by providing explicit overrides.

const REGISTRY_PATH = "res://addons/mu_tools/resources/mu_model_registry.json"

const EFFECT_KEYWORDS = ["light", "fire", "effect", "water", "spout"]
const SHADOW_KEYWORDS = ["shadow"]
const CUTOUT_KEYWORDS = ["tree", "grass", "leaf", "fence", "gate", "barred", "straw", "bamboo", "etc"]
const IGNORE_ALPHA_KEYWORDS = ["reagon"] # Exclude dragon from transparency heuristic

const OBJECT_MAP_W1 = {
	0: "Tree%02d",	   # 0-12
	20: "Grass%02d",	  # 20-27
	30: "Stone%02d",	  # 30-34
	40: "StoneStatue%02d", # 40-42
	43: "SteelStatue01",
	44: "Tomb%02d",	   # 44-46
	50: "FireLight%02d",  # 50-51
	52: "Bonfire01",
	55: "DoungeonGate01",
	56: "MerchantAnimal%02d", # 56-57
	58: "TreasureDrum01",
	59: "TreasureChest01",
	60: "Ship01",
	65: "SteelWall%02d",  # 65-67
	68: "SteelDoor01",
	69: "StoneWall%02d",  # 69-74
	75: "StoneMuWall%02d", # 75-78
	80: "Bridge01",
	81: "Fence%02d",	  # 81-84
	85: "BridgeStone01",
	90: "StreetLight01",
	91: "Cannon%02d",	 # 91-93
	95: "Curtain01",
	96: "Sign%02d",	   # 96-97
	98: "Carriage%02d",   # 98-101
	102: "Straw%02d",	 # 102-103
	105: "Waterspout01",
	106: "Well%02d",	  # 106-109
	110: "Hanging01",
	111: "Stair01",
	115: "House%02d",	 # 115-119
	120: "Tent01",
	121: "HouseWall%02d", # 121-126
	127: "HouseEtc%02d",  # 127-129
	130: "Light%02d",	 # 130-132
	133: "PoseBox01",
	140: "Furniture%02d", # 140-146
	150: "Candle01",
	151: "Beer%02d"	   # 151-153
}

static var _data: Dictionary = {}

static func load_registry() -> bool:
	if not FileAccess.file_exists(REGISTRY_PATH):
		print("[MU Registry] No registry file found. Using heuristics.")
		return false
		
	var file = FileAccess.open(REGISTRY_PATH, FileAccess.READ)
	var content = file.get_as_text()
	file.close()
	
	var json = JSON.new()
	if json.parse(content) == OK:
		_data = json.get_data()
		return true
	else:
		push_error("[MU Registry] Failed to parse registry: " + json.get_error_message())
		return false

## Returns metadata for a specific BMD path.
static func get_metadata(bmd_path: String) -> Dictionary:
	if _data.is_empty():
		load_registry()
		
	var model_name = bmd_path.get_file().get_basename()
	if _data.has(model_name):
		return _data[model_name]
	
	# Default metadata (empty triggers heuristics)
	return {}

## Helper to resolve bind pose action index.
static func get_bind_pose_action(bmd_path: String, default: int = -1) -> int:
	var meta = get_metadata(bmd_path)
	if meta.has("bind_pose_action"):
		return int(meta["bind_pose_action"])
	return default

## Helper to resolve rotation override (Euler degrees).
static func get_rotation_override(bmd_path: String) -> Vector3:
	var meta = get_metadata(bmd_path)
	if meta.has("rotation_override"):
		var rot_array = meta["rotation_override"]
		if rot_array is Array and rot_array.size() == 3:
			return Vector3(rot_array[0], rot_array[1], rot_array[2])
	return Vector3.ZERO

static func get_texture_tweak(texture_name: String) -> Dictionary:
	if _data.is_empty():
		load_registry()
	
	if _data.has("texture_tweaks") and _data["texture_tweaks"].has(texture_name):
		return _data["texture_tweaks"][texture_name]
	return {}

static func get_material_tweak(texture_name: String) -> Dictionary:
	if _data.is_empty():
		load_registry()
	
	if _data.has("material_tweaks") and _data["material_tweaks"].has(texture_name):
		return _data["material_tweaks"][texture_name]
	return {}

static func get_object_path(type: int, world_id: int = 1) -> String:
	var pattern = ""
	var map = {}
	if world_id == 1:
		map = OBJECT_MAP_W1
	
	# Find base type in map
	var base_type = -1
	if map.has(type):
		base_type = type
	else:
		# Check range
		var keys = map.keys()
		keys.sort()
		for k in keys:
			if k <= type:
				base_type = k
			else:
				break
	
	if base_type != -1:
		var p = map[base_type]
		if "%02d" in p:
			var idx = (type - base_type) + 1
			pattern = p % idx
		else:
			pattern = p
			
	if pattern != "":
		# Construct absolute resource path
		# Assuming standard layout: res://reference/MuMain/src/bin/Data/Object{WorldID}/{Name}.bmd
		return "res://reference/MuMain/src/bin/Data/Object%d/%s.bmd" % [world_id, pattern]
		
	return ""
