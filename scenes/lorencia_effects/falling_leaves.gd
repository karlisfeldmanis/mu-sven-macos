extends GPUParticles3D

const MUTextureLoader = preload("res://addons/mu_tools/mu_texture_loader.gd")
const MUFileUtil = preload("res://addons/mu_tools/mu_file_util.gd")

func _ready() -> void:
	name = "FallingLeaves"
	
	# Emitting settings
	amount = 100
	lifetime = 10.0
	preprocess = 5.0
	
	# Process Material
	var p_mat = ParticleProcessMaterial.new()
	p_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	p_mat.emission_box_extents = Vector3(100, 20, 100) # Area around player
	
	p_mat.gravity = Vector3(0, -1.0, 0) # Slow fall
	p_mat.direction = Vector3(-1, 0, 0) # Wind factor
	p_mat.spread = 15.0
	p_mat.initial_velocity_min = 2.0
	p_mat.initial_velocity_max = 5.0
	
	p_mat.angular_velocity_min = 20.0
	p_mat.angular_velocity_max = 60.0
	
	p_mat.angle_min = 0.0
	p_mat.angle_max = 360.0
	
	p_mat.scale_min = 0.3
	p_mat.scale_max = 0.6
	
	self.process_material = p_mat
	
	# Mesh and Material
	var plane_mesh = QuadMesh.new()
	plane_mesh.size = Vector2(1, 1)
	
	var leaf_mat = ShaderMaterial.new()
	leaf_mat.shader = load("res://scenes/lorencia_effects/shaders/leaf_flutter.gdshader")
	
	# Try standard MU paths first
	var tex_paths = [
		"res://assets/lorencia/textures/leaf01.png", # Custom asset?
		"res://reference/MuMain/src/bin/Data/World1/leaf01.tga",
		"res://reference/MuMain/src/bin/Data/World1/leaf01.ozj"
	]
	
	var leaf_tex: Texture2D = null
	for path in tex_paths:
		if MUFileUtil.file_exists(path):
			# Use appropriate loader
			if path.ends_with(".png"):
				leaf_tex = load(path)
			else:
				leaf_tex = MUTextureLoader.load_mu_texture(path)
			
			if leaf_tex:
				break
				
	if not leaf_tex:
		print("  [FallingLeaves] Could not find leaf texture, using fallback.")
		# Don't error out, just skip texture (particles will be white squares or use default)
	else:
		leaf_mat.set_shader_parameter("leaf_texture", leaf_tex)
	
	self.draw_pass_1 = plane_mesh
	self.draw_passes = 1
	self.material_override = leaf_mat
	
	# Follow logic hint: Parent this to the player or camera rig
	print("[FallingLeaves] Particles ready")

func _process(_delta: float) -> void:
	# Keep the emission box centered on the camera's X-Z plane
	var cam = get_viewport().get_camera_3d()
	if cam:
		position = cam.global_position
		position.y += 20.0 # Emit from above
