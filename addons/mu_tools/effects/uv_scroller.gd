extends Node

@export var speed: Vector2 = Vector2(0, -0.1)

func _process(delta):
	var parent = get_parent()
	if not parent is MeshInstance3D: return
	
	# Match Sven's scrolling: v += WorldTime % 1000 * 0.001
	# We'll use a shader parameter if possible, otherwise shift UVs
	for i in range(parent.get_surface_override_material_count()):
		var mat = parent.get_surface_override_material(i)
		if mat is ShaderMaterial:
			var current = mat.get_shader_parameter("uv_offset")
			if current == null: current = Vector2.ZERO
			mat.set_shader_parameter("uv_offset", current + speed * delta)
		elif mat is StandardMaterial3D:
			mat.uv1_offset += Vector3(speed.x, speed.y, 0) * delta
