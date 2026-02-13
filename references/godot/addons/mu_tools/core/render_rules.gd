extends RefCounted

# Centralized Rendering Rules for MU Online Models
# Maps ModelID (folder/basename) to specialized rendering flags

const RENDER_CHROME = 0x00000004
const RENDER_WAVE   = 0x00000400
const RENDER_BRIGHT = 0x00000040

const RULES = {
	"Player/Player": {
		"parts": {
			"wings": {"chrome": true, "bright": true}
		}
	},
	"Object1/Water01": {
		"all_meshes": {"wave": true, "bright": true}
	},
	"Object1/Curtain01": {
		"mesh_overrides": {
			1: {
				"wave": true,
				"wave_stiffness_top": 1.0,
				"wave_stiffness_center": 1.0
			}
		}
	},
	"Object1/StoneWall04": {
		"mesh_overrides": {
			1: {"wave": true}
		}
	},
	"Object1/StoneWall06": {
		"mesh_overrides": {
			1: {"wave": true}
		}
	},
	"Object1/Fenrir01": {
		"all_meshes": {"chrome": true}
	},
	"Object1/DarkHorse01": {
		"mesh_overrides": {
			12: {"chrome": true, "bright": true},
			13: {"chrome": true, "bright": true},
			14: {"chrome": true, "bright": true},
			15: {"chrome": true, "bright": true}
		}
	},
	# Lorencia (WD_0) Rules — keys must match BMD filenames
	"Object1/Waterspout01": {
		"mesh_overrides": {
			3: {"bright": true, "scroll": [0.0, 1.0], "sawtooth": true}
		}
	},
	"Object1/Bonfire01": {
		"mesh_overrides": {1: {"bright": true, "pulsing": true}}
	},
	"Object1/StreetLight01": {
		"mesh_overrides": {1: {"bright": true, "pulsing": true}}
	},
	"Object1/Candle01": {
		"mesh_overrides": {1: {"bright": true, "pulsing": true}}
	},
	"Object1/Tree01": { "all_meshes": {} },
	"Object1/Tree02": { "all_meshes": {} },
	"Object1/Tree03": { "all_meshes": {} },
	"Object1/Tree04": { "all_meshes": {} },
	"Object1/Tree05": { "all_meshes": {} },
	"Object1/Tree06": { "all_meshes": {} },
	"Object1/Tree07": { "all_meshes": {} },
	"Object1/Tree08": { "all_meshes": {} },
	"Object1/Tree09": { "all_meshes": {} },
	"Object1/Tree10": { "all_meshes": {} },
	"Object1/Tree11": { "all_meshes": {} },
	"Object1/Tree12": { "all_meshes": {} },
	"Object1/Tree13": { "all_meshes": {} },
	"Object1/Grass01": { "all_meshes": {} },
	"Object1/Grass02": { "all_meshes": {} },
	"Object1/Grass03": { "all_meshes": {} },
	"Object1/Grass04": { "all_meshes": {} },
	"Object1/Grass05": { "all_meshes": {} },
	"Object1/Grass06": { "all_meshes": {} },
	"Object1/Grass07": { "all_meshes": {} },
	"Object1/Grass08": { "all_meshes": {} },
	"Object1/House03": {
		"mesh_overrides": {
			4: {"bright": true, "pulsing": true, "jitter": true},
			5: {"bright": true}
		}
	},
	"Object1/House04": {
		"mesh_overrides": {
			7: {"bright": true},
			8: {"bright": true, "scroll": [0.0, 1.0], "sawtooth": true}
		}
	},
	"Object1/House05": {
		"mesh_overrides": {2: {"bright": true, "scroll": [0.0, 1.0], "sawtooth": true}}
	},
	"Object1/HouseWall02": {
		"mesh_overrides": {4: {"bright": true, "pulsing": true, "jitter": true}}
	},
	"Object1/Carriage01": {
		"mesh_overrides": {2: {"bright": true}}
	},
	# Devias (WD_2) Rules
	"Object2/Warp": { "mesh_overrides": {0: {"bright": true}} },
	"Object3/Warp": { "mesh_overrides": {0: {"bright": true}} },
	"Object2/Object01": { # Bridge/Water in Devias often uses Additive
		"mesh_overrides": {3: {"bright": true}}
	},
	# Wings (RENDER_CHROME | RENDER_BRIGHT)
	"Item/Wing01": { "all_meshes": {"chrome": true, "bright": true} },
	"Item/Wing02": { "all_meshes": {"chrome": true, "bright": true} },
	"Item/Wing03": { "all_meshes": {"chrome": true, "bright": true} },
	"Item/Wing04": { "all_meshes": {"chrome": true, "bright": true} }
}

static func get_rules_for_model(path: String) -> Dictionary:
	# Extract "Object1/Tree01" from any path
	var path_normalized = path.replace("\\", "/")
	var parts = path_normalized.split("/")
	if parts.size() < 2: return {}
	
	var folder = parts[parts.size() - 2]
	var file = parts[parts.size() - 1].get_basename()
	var key = folder + "/" + file
	
	return RULES.get(key, {})

static func apply_rules_to_mesh(mesh_idx: int, model_path: String, shader_material: ShaderMaterial):
	var rules = get_rules_for_model(model_path)
	if rules.is_empty(): return
	
	var flags = {}
	
	# Global overrides
	if rules.has("all_meshes"):
		flags = rules["all_meshes"]
	
	# Specific mesh overrides
	if rules.has("mesh_overrides") and rules["mesh_overrides"].has(mesh_idx):
		flags = rules["mesh_overrides"][mesh_idx]
		
	# Apply to shader
	if flags.has("chrome"):
		shader_material.set_shader_parameter("use_chrome", flags["chrome"])
	if flags.has("wave"):
		shader_material.set_shader_parameter("use_wave", flags["wave"])
	if flags.has("bright"):
		shader_material.set_shader_parameter("use_bright", flags["bright"])
	if flags.has("pulsing"):
		shader_material.set_shader_parameter("use_pulsing", flags["pulsing"])
	if flags.has("jitter"):
		shader_material.set_shader_parameter("use_jitter", flags["jitter"])
	if flags.has("wave_stiffness_top"):
		shader_material.set_shader_parameter("wave_stiffness_top", flags["wave_stiffness_top"])
	if flags.has("wave_stiffness_center"):
		shader_material.set_shader_parameter(
			"wave_stiffness_center", flags["wave_stiffness_center"]
		)
	if flags.has("sawtooth"):
		shader_material.set_shader_parameter("use_sawtooth", flags["sawtooth"])
	if flags.has("scroll"):
		shader_material.set_shader_parameter("scroll_speed_u", flags["scroll"][0])
		shader_material.set_shader_parameter("scroll_speed_v", flags["scroll"][1])
