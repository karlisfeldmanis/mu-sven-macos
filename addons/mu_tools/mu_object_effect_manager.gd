extends Node

class_name MUObjectEffectManager

# Model IDs for Lorencia
const MODEL_WATERSPOUT = 105
const MODEL_MERCHANT_ANIMAL01 = 56
const MODEL_HOUSE_01 = 115 # Base for House01-05
const MODEL_STREET_LIGHT = 90
const MODEL_CANDLE = 150

const UV_SCROLLER_SCRIPT = preload("res://addons/mu_tools/effects/uv_scroller.gd")
const LIGHT_FLICKER_SCRIPT = preload("res://addons/mu_tools/effects/light_flicker.gd")
const ADDITIVE_SHADER = preload("res://core/shaders/mu_additive.gdshader")
const WIND_SHADER = preload("res://core/shaders/mu_wind.gdshader")

static func apply_effects(node: Node3D, type: int, world_id: int):
	# Default: Apply full opacity
	_apply_object_alpha(node, 1.0)
	
	if world_id != 1: return # Focus on Lorencia for now
	
	match type:
		MODEL_WATERSPOUT:
			_setup_fountain(node)
		MODEL_MERCHANT_ANIMAL01, MODEL_MERCHANT_ANIMAL01 + 1:
			_setup_merchant_animal(node)
		115, 116, 117, 118, 119: # Houses 1-5
			_setup_house(node, type)
		121, 122: # House Wall 01/02
			_setup_house(node, type)
		MODEL_STREET_LIGHT:
			_setup_light(node, Color(1.0, 0.8, 0.6), 5.0)
		MODEL_CANDLE:
			_setup_light(node, Color(1.0, 0.6, 0.2), 3.0)
		20, 21, 22, 23, 24, 25, 26, 27: # Grass01 - Grass08
			_setup_grass(node)

static func _setup_grass(node: Node3D):
	# Adjust scale - User requested grass to be smaller
	# Multiply EXISTING scale by 0.5 (Object Grass BMDs are huge)
	# and add a bit of random variation to avoid "robotic" same-size look
	var variation = randf_range(0.8, 1.2)
	node.scale *= 0.5 * variation
	
	# Apply wind shader to all meshes
	for child in node.get_children():
		if child is MeshInstance3D:
			for i in range(child.get_surface_override_material_count()):
				var old_mat = child.get_surface_override_material(i)
				if old_mat is StandardMaterial3D:
					var new_mat = ShaderMaterial.new()
					new_mat.shader = WIND_SHADER
					new_mat.set_shader_parameter("texture_albedo", old_mat.albedo_texture)
					# Wind parameters are now handled by global uniforms in shader
					new_mat.set_shader_parameter("global_alpha", 0.8) # Slight opacity
					child.set_surface_override_material(i, new_mat)

# ... (rest of the file helpers)

static func _apply_object_alpha(node: Node, alpha: float):
	for child in node.get_children():
		if child is MeshInstance3D:
			for i in range(child.get_surface_override_material_count()):
				var mat = child.get_surface_override_material(i)
				if mat is ShaderMaterial:
					mat.set_shader_parameter("global_alpha", alpha)
				elif mat is StandardMaterial3D:
					# Ensure transparency is enabled if alpha < 1.0
					if alpha < 1.0:
						mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
						mat.albedo_color.a = alpha
		elif child.get_child_count() > 0:
			_apply_object_alpha(child, alpha)

static func _setup_fountain(node: Node3D):
	# Sven: mesh index 3 is the water spout that scrolls and uses additive blending.
	var water_mesh = node.get_child(3) if node.get_child_count() > 3 else null
	if water_mesh and water_mesh is MeshInstance3D:
		# Add UV Scrolling
		var scroller = Node.new()
		scroller.name = "FountainScroller"
		scroller.set_script(UV_SCROLLER_SCRIPT)
		water_mesh.add_child(scroller)
		
		# Set Additive Blending
		for i in range(water_mesh.get_surface_override_material_count()):
			var mat = water_mesh.get_surface_override_material(i)
			if mat is ShaderMaterial:
				mat.shader = ADDITIVE_SHADER
			elif mat is StandardMaterial3D: # Convert if needed, though usually handled
				pass # For now assume effect applied correctly or shader already exists?
				# Actually we should convert like grass if we want to force ShaderMaterial
				# But let's leave existing logic unless broken.
	
	# ... (rest of setup_fountain)

# ... (Existing setup functions)

	
	# Smoke particles at bones 1 and 4 in Sven (Wait for skeleton)

static func _setup_merchant_animal(node: Node3D):
	# Sven: eye glows
	_setup_light(node, Color(1.0, 0.4, 0.2), 2.0)

static func _setup_house(node: Node3D, type: int):
	if type == 117 or type == 118: # House 3/4
		# Window UV scrolling
		var window_mesh = node.get_child(3) if node.get_child_count() > 3 else null
		if window_mesh and window_mesh is MeshInstance3D:
			var scroller = Node.new()
			scroller.name = "WindowScroller"
			scroller.set_script(UV_SCROLLER_SCRIPT)
			window_mesh.add_child(scroller)
			
			# Additive windows
			for i in range(window_mesh.get_surface_override_material_count()):
				var mat = window_mesh.get_surface_override_material(i)
				if mat is ShaderMaterial:
					mat.shader = ADDITIVE_SHADER
					
	elif type == 116 or type == 122: # House 2 / Wall 1
		# Flickering lights
		_setup_light(node, Color(1.0, 0.9, 0.5), 1.5)

static func _setup_light(node: Node3D, color: Color, energy: float):
	var light = node.get_node_or_null("DynamicLight")
	if not light:
		light = OmniLight3D.new()
		light.name = "DynamicLight"
		light.light_color = color
		light.light_energy = energy
		light.omni_range = 8.0
		light.shadow_enabled = true
		node.add_child(light)
		
		var flicker = Node.new()
		flicker.name = "Flicker"
		flicker.set_script(LIGHT_FLICKER_SCRIPT)
		light.add_child(flicker)
