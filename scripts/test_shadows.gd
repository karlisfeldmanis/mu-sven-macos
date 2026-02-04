extends Node3D

## Simple Shadow Test Scene
## Creates a cube with a light to verify shadows work

func _ready():
	print("=== Shadow Test ===")
	
	# Camera
	var camera = Camera3D.new()
	camera.position = Vector3(5, 5, 5)
	camera.look_at(Vector3.ZERO)
	add_child(camera)
	
	# Ground plane
	var ground = MeshInstance3D.new()
	var plane_mesh = PlaneMesh.new()
	plane_mesh.size = Vector2(20, 20)
	ground.mesh = plane_mesh
	ground.position = Vector3(0, -1, 0)
	ground.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(ground)
	
	# Test cube that SHOULD cast shadow
	var cube = MeshInstance3D.new()
	var box_mesh = BoxMesh.new()
	cube.mesh = box_mesh
	cube.position = Vector3(0, 0.5, 0)
	cube.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(cube)
	
	print("Cube shadow casting: ", cube.cast_shadow)
	
	# Directional light with shadows
	var sun = DirectionalLight3D.new()
	sun.transform.basis = Basis.looking_at(Vector3(-1, -1, -1))
	sun.light_energy = 1.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.directional_shadow_max_distance = 100.0
	add_child(sun)
	
	print("Light shadow enabled: ", sun.shadow_enabled)
	print("Shadow mode: ", sun.directional_shadow_mode)
	print("Renderer: ", RenderingServer.get_video_adapter_name())
	
	# Quit after 3 seconds
	await get_tree().create_timer(3.0).timeout
	print("If you see a shadow under the cube, shadows are working!")
	get_tree().quit()
