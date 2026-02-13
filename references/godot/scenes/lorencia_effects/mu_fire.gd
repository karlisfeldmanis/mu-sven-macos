@tool
extends GPUParticles3D

class_name MUFire

static func create(parent: Node3D, position: Vector3, type: int = 0, use_local_pos: bool = false) -> GPUParticles3D:
	var fire = load("res://scenes/lorencia_effects/mu_fire.gd").new()
	fire.name = "MUFire"
	parent.add_child(fire)
	if use_local_pos:
		fire.position = position
	else:
		fire.global_position = position
	fire.setup(type)
	return fire

func setup(type: int):
	# Material Setup
	var mat = ShaderMaterial.new()
	mat.shader = load("res://scenes/lorencia_effects/shaders/mu_fire.gdshader")
	
	var tex_path = "res://reference/MuMain/src/bin/Data/Effect/Fire01.OZJ"
	if type >= 1:
		tex_path = "res://reference/MuMain/src/bin/Data/Effect/smoke01.OZJ"
	
	const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")
	var tex: Texture2D = MUTextureLoader.load_mu_texture(tex_path)
	mat.set_shader_parameter("fire_texture", tex)
	mat.set_shader_parameter("is_smoke", type >= 1)
	
	# Process Material (Particles behavior) - SVEN Parity
	var pm = ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.08
	pm.direction = Vector3(0, 1, 0)
	pm.spread = 15.0
	pm.gravity = Vector3(0, 1.2, 0)
	
	if type == 0:
		# BITMAP_FIRE: LifeTime 24, Scale 1.28-1.92 approx
		pm.initial_velocity_min = 0.4
		pm.initial_velocity_max = 0.8
		pm.scale_min = 0.6
		pm.scale_max = 0.9
		self.lifetime = 0.8 # approx 24 frames @ 30fps
	elif type == 1:
		# BITMAP_SMOKE: LifeTime 16, Scale 0.48-0.8
		pm.initial_velocity_min = 0.3
		pm.initial_velocity_max = 0.6
		pm.scale_min = 0.4
		pm.scale_max = 0.7
		self.lifetime = 0.5 # approx 16 frames @ 30fps
	else:
		# BITMAP_SMOKE Type 2: LifeTime 50, Scale 0.64-1.28
		pm.initial_velocity_min = 0.2
		pm.initial_velocity_max = 0.4
		pm.scale_min = 0.6
		pm.scale_max = 1.2
		pm.gravity = Vector3(0, 0.6, 0) # Slower rise
		self.lifetime = 1.6 # approx 50 frames @ 30fps
		
	pm.angular_velocity_min = -35.0
	pm.angular_velocity_max = 35.0
	
	# Fade out
	var alpha_curve = Curve.new()
	alpha_curve.add_point(Vector2(0, 0))
	alpha_curve.add_point(Vector2(0.2, 1.0))
	alpha_curve.add_point(Vector2(0.7, 0.8))
	alpha_curve.add_point(Vector2(1.0, 0.0))
	var alpha_tex = CurveTexture.new()
	alpha_tex.curve = alpha_curve
	pm.alpha_curve = alpha_tex
	
	self.process_material = pm
	
	# Mesh Setup
	var quad = QuadMesh.new()
	quad.size = Vector2(0.2, 0.2)
	if type >= 1:
		quad.size = Vector2(0.4, 0.4) # Smoke is larger
	quad.material = mat
	self.draw_pass_1 = quad
	
	# Settings
	self.amount = 24 if type != 2 else 40
	self.randomness = 0.4
	self.fixed_fps = 0
	self.visibility_aabb = AABB(Vector3(-0.5, -0.5, -0.5), Vector3(1, 2, 1))
	self.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	
	# Point Light
	if type == 0:
		var light = OmniLight3D.new()
		light.light_color = Color(1.0, 0.5, 0.1)
		light.light_energy = 1.5
		light.omni_range = 2.0
		light.position = Vector3(0, 0.15, 0)
		add_child(light)
	elif type == 2:
		# Very subtle light for big smoke
		var light = OmniLight3D.new()
		light.light_color = Color(0.2, 0.2, 0.25) # Cool smoke glow
		light.light_energy = 0.5
		light.omni_range = 1.5
		light.position = Vector3(0, 0.5, 0)
		add_child(light)
	
	print("[MUFire] Initialized fire type %d at %s" % [type, str(global_position)])
