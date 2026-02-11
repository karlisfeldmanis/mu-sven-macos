@tool
extends Node

class_name MUModelRegistry

## Centralized registry for MU Online model metadata.
## Resolves heuristics by providing explicit overrides.

const REGISTRY_PATH = "res://addons/mu_tools/resources/mu_model_registry.json"

const EFFECT_KEYWORDS = ["light", "fire", "effect"]
const SHADOW_KEYWORDS = ["shadow"]
const CUTOUT_KEYWORDS = [
	"tree", "grass", "leaf", "fence", "gate", "barred", "straw", "bamboo", 
	"ston", "web", "etc"
]
const IGNORE_ALPHA_KEYWORDS = ["reagon"] # Exclude dragon from transparency heuristic

enum ModelType {
	MODEL_WORLD_OBJECT = 0,
	MODEL_TREE01 = 0,
	MODEL_GRASS01 = 20,
	MODEL_STONE01 = 30,
	MODEL_STONE_STATUE01 = 40,
	MODEL_STONE_STATUE02 = 41,
	MODEL_STONE_STATUE03 = 42,
	MODEL_STEEL_STATUE = 43,
	MODEL_TOMB01 = 44,
	MODEL_TOMB02 = 45,
	MODEL_TOMB03 = 46,
	MODEL_FIRE_LIGHT01 = 50,
	MODEL_FIRE_LIGHT02 = 51,
	MODEL_BONFIRE = 52,
	MODEL_DUNGEON_GATE = 55,
	MODEL_MERCHANT_ANIMAL01 = 56,
	MODEL_MERCHANT_ANIMAL02 = 57,
	MODEL_TREASURE_DRUM = 58,
	MODEL_TREASURE_CHEST = 59,
	MODEL_SHIP = 60,
	MODEL_STEEL_WALL01 = 65,
	MODEL_STEEL_WALL02 = 66,
	MODEL_STEEL_WALL03 = 67,
	MODEL_STEEL_DOOR = 68,
	MODEL_STONE_WALL01 = 69,
	MODEL_STONE_WALL02 = 70,
	MODEL_STONE_WALL03 = 71,
	MODEL_STONE_WALL04 = 72,
	MODEL_STONE_WALL05 = 73,
	MODEL_STONE_WALL06 = 74,
	MODEL_MU_WALL01 = 75,
	MODEL_MU_WALL02 = 76,
	MODEL_MU_WALL03 = 77,
	MODEL_MU_WALL04 = 78,
	MODEL_BRIDGE = 80,
	MODEL_FENCE01 = 81,
	MODEL_FENCE02 = 82,
	MODEL_FENCE03 = 83,
	MODEL_FENCE04 = 84,
	MODEL_BRIDGE_STONE = 85,
	MODEL_STREET_LIGHT = 90,
	MODEL_CANNON01 = 91,
	MODEL_CANNON02 = 92,
	MODEL_CANNON03 = 93,
	MODEL_CURTAIN = 95,
	MODEL_SIGN01 = 96,
	MODEL_SIGN02 = 97,
	MODEL_CARRIAGE01 = 98,
	MODEL_CARRIAGE02 = 99,
	MODEL_CARRIAGE03 = 100,
	MODEL_CARRIAGE04 = 101,
	MODEL_STRAW01 = 102,
	MODEL_STRAW02 = 103,
	MODEL_WATERSPOUT = 105,
	MODEL_WELL01 = 106,
	MODEL_WELL02 = 107,
	MODEL_WELL03 = 108,
	MODEL_WELL04 = 109,
	MODEL_HANGING = 110,
	MODEL_STAIR = 111,
	MODEL_HOUSE01 = 115,
	MODEL_HOUSE02 = 116,
	MODEL_HOUSE03 = 117,
	MODEL_HOUSE04 = 118,
	MODEL_HOUSE05 = 119,
	MODEL_TENT = 120,
	MODEL_HOUSE_WALL01 = 121,
	MODEL_HOUSE_WALL02 = 122,
	MODEL_HOUSE_WALL03 = 123,
	MODEL_HOUSE_WALL04 = 124,
	MODEL_HOUSE_WALL05 = 125,
	MODEL_HOUSE_WALL06 = 126,
	MODEL_HOUSE_ETC01 = 127,
	MODEL_HOUSE_ETC02 = 128,
	MODEL_HOUSE_ETC03 = 129,
	MODEL_LIGHT01 = 130,
	MODEL_LIGHT02 = 131,
	MODEL_LIGHT03 = 132,
	MODEL_POSE_BOX = 133,
	MODEL_FURNITURE01 = 140,
	MODEL_FURNITURE02 = 141,
	MODEL_FURNITURE03 = 142,
	MODEL_FURNITURE04 = 143,
	MODEL_FURNITURE05 = 144,
	MODEL_FURNITURE06 = 145,
	MODEL_FURNITURE07 = 146,
	MODEL_CANDLE = 150,
	MODEL_BEER01 = 151,
	MODEL_BEER02 = 152,
	MODEL_BEER03 = 153,
	MODEL_BIRD01 = 154, # approx, based on enum offset
}



const WORLD_MAPS = {
	0: { # Lorencia
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
}

# Objects that are efficient to batch as MultiMesh
const BATCHABLE_TYPES = [
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, # Trees
	20, 21, 22, 23, 24, 25, 26, 27, # Grass
	65, 66, 67, # SteelWall
	69, 70, 71, 72, 73, 74, # StoneWall
	75, 76, 77, 78, # StoneMuWall
	81, 82, 83, 84, # Fence
	121, 122, 123, 124, 125, 126, # HouseWall
]

const WALL_TYPES = [
	65, 66, 67, # SteelWall
	69, 70, 71, 72, 73, 74, # StoneWall
	75, 76, 77, 78, # StoneMuWall
	121, 122, 123, 124, 125, 126, # HouseWall
]

static func is_wall(type: int, world_id: int) -> bool:
	var path = get_object_path(type, world_id)
	if path == "": return false
	var low = path.to_lower()
	return ("wall" in low or "fence" in low or "house" in low) and not "statue" in low

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
		
	push_error("[MU Registry] Failed to parse registry: " + json.get_error_message())
	return false

## Returns all available BMD models in the Data directory (Dynamic Discovery)
static func get_all_available_models() -> Array[String]:
	var models: Array[String] = []
	var data_dir = "res://reference/MuMain/src/bin/Data/"
	var dir = DirAccess.open(data_dir)
	if not dir:
		return models
		
	dir.list_dir_begin()
	var sub_dir = dir.get_next()
	while sub_dir != "":
		if dir.current_is_dir() and sub_dir.begins_with("Object"):
			var obj_path = data_dir.path_join(sub_dir)
			var obj_dir = DirAccess.open(obj_path)
			if obj_dir:
				obj_dir.list_dir_begin()
				var file_name = obj_dir.get_next()
				while file_name != "":
					if not obj_dir.current_is_dir() and file_name.to_lower().ends_with(".bmd"):
						models.append(obj_path.path_join(file_name))
					file_name = obj_dir.get_next()
				obj_dir.list_dir_end()
		sub_dir = dir.get_next()
	dir.list_dir_end()
	
	models.sort()
	return models

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
	if meta.has("forced_bind_pose_action"):
		return int(meta["forced_bind_pose_action"])
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

static func get_blend_mesh_index(bmd_path: String) -> int:
	var meta = get_metadata(bmd_path)
	return int(meta.get("blend_mesh_index", -1))

static func get_velocity(bmd_path: String) -> float:
	var meta = get_metadata(bmd_path)
	return float(meta.get("velocity", 0.0))

static func get_hidden_mesh(bmd_path: String) -> int:
	var meta = get_metadata(bmd_path)
	return int(meta.get("hidden_mesh", -1))

static func get_collision_range(bmd_path: String) -> float:
	var meta = get_metadata(bmd_path)
	return float(meta.get("collision_range", 0.0))

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
	var world_map = WORLD_MAPS.get(world_id, {})
	
	# Find base type in map
	var base_type = -1
	if world_map.has(type):
		base_type = type
	else:
		# Check range
		var keys = world_map.keys()
		keys.sort()
		keys.reverse() # Find highest key <= type
		for k in keys:
			if k <= type:
				base_type = k
				break
	
	if base_type != -1:
		var p = world_map[base_type]
		if "%02d" in p:
			var idx = (type - base_type) + 1
			pattern = p % idx
		else:
			pattern = p
			
	if pattern != "":
		# Construct absolute resource path
		var dir_idx = world_id + 1
		
		# 1. Try Source BMD (Original Assets - Source of Truth)
		var bmd_path = "res://reference/MuMain/src/bin/Data/Object%d/%s.bmd" % [dir_idx, pattern]
		if FileAccess.file_exists(bmd_path):
			return bmd_path
			
		# 2. Try exported TSCN (Pre-baked Godot Cache) - Fallback
		var tscn_path = "res://assets/models/Object%d/%s.tscn" % [dir_idx, pattern]
		if FileAccess.file_exists(tscn_path):
			return tscn_path
			
		# 3. Try exported OBJ (Legacy Export)
		var obj_path = "res://assets/models/Object%d/%s.obj" % [dir_idx, pattern]
		if FileAccess.file_exists(obj_path):
			return obj_path
		
	return ""

static func is_batchable(type: int) -> bool:
	return type in BATCHABLE_TYPES
