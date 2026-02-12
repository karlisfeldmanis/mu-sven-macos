@tool
extends GPUParticles3D

class_name MUFire

static func create(parent: Node3D, position: Vector3, type: int = 0) -> GPUParticles3D:
	var fire = load("res://scenes/lorencia_effects/mu_fire.gd").new()
	fire.name = "MUFire"
	parent.add_child(fire)
	fire.global_position = position
	fire.setup(type)
	return fire

func setup(type: int):
	# Material Setup
	var mat = ShaderMaterial.new()
	mat.shader = load("res://scenes/lorencia_effects/shaders/mu_fire.gdshader")
	
	var tex_path = "res://reference/MuMain/src/bin/Data/Object1/fire01.ozj"
	if type == 1:
		tex_path = "res://reference/MuMain/src/bin/Data/Object1/fire02.ozj"
	
	const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")
	var tex: Texture2D = MUTextureLoader.load_mu_texture(tex_path)
	mat.set_shader_parameter("fire_texture", tex)
	
	# Process Material (Particles behavior) - Balanced settings
	var pm = ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.08
	pm.direction = Vector3(0, 1, 0)
	pm.spread = 12.0
	pm.gravity = Vector3(0, 1.8, 0)
	pm.initial_velocity_min = 0.4
	pm.initial_velocity_max = 0.8
	pm.angular_velocity_min = -35.0
	pm.angular_velocity_max = 35.0
	pm.linear_accel_min = 0.4
	pm.linear_accel_max = 0.8
	pm.scale_min = 0.4  # Smaller for realistic flames
	pm.scale_max = 0.7  # Smaller for realistic flames
	
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
	
	# Mesh Setup - Small particles for realistic fire
	var quad = QuadMesh.new()
	quad.size = Vector2(0.12, 0.18)  # Much smaller for realistic flames
	quad.material = mat
	self.draw_pass_1 = quad
	
	# Settings - More particles to compensate for smaller size
	self.amount = 16  # More particles for density
	self.lifetime = 0.6  # Shorter lifetime for flickering effect
	self.randomness = 0.4
	self.fixed_fps = 0 # Match render FPS
	self.visibility_aabb = AABB(Vector3(-0.5, -0.5, -0.5), Vector3(1, 2, 1))
	self.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	
	# Point Light for glow
	var light = OmniLight3D.new()
	light.light_color = Color(1.0, 0.5, 0.1)
	light.light_energy = 1.5
	light.omni_range = 2.0  # Moderate range
	light.position = Vector3(0, 0.15, 0)
	add_child(light)
	
	print("[MUFire] Initialized fire at ", global_position)
