extends Node

class_name MUObjectEffectManager

# Model IDs for Lorencia
const MODEL_WATERSPOUT = 105
const MODEL_MERCHANT_ANIMAL01 = 56
const MODEL_MERCHANT_ANIMAL02 = 57
const MODEL_HOUSE_01 = 115 
const MODEL_STREET_LIGHT = 90
const MODEL_CANDLE = 150

const MUModelRegistry = preload("res://addons/mu_tools/core/registry.gd")
const MU_FIRE_SCRIPT = preload("res://scenes/lorencia_effects/mu_fire.gd")
const UV_SCROLLER_SCRIPT = preload("res://addons/mu_tools/effects/uv_scroller.gd")
const LIGHT_FLICKER_SCRIPT = preload("res://addons/mu_tools/effects/light_flicker.gd")
const ADDITIVE_SHADER = preload("res://addons/mu_tools/shaders/mu_additive.gdshader")
const MUMaterialHelper = preload("res://addons/mu_tools/rendering/material_helper.gd")
const MODEL_TYPE = MUModelRegistry.ModelType

static func update_globals(_player_node: Node3D):
	pass

static func apply_effects(node: Node3D, type: int, world_id: int):
	_apply_object_alpha(node, 1.0)
	
	if world_id != 0: return

	# Apply BlendMesh additive blending for registered models
	var path = node.get_meta("mu_path", "")
	var blend_idx = MUModelRegistry.get_blend_mesh_index(path)
	if blend_idx != -1:
		_apply_blend_mesh(node, blend_idx)

	match type:
		0,1,2,3,4,5,6,7,8,9,10,11,12: # Trees
			var velocity = (1.0 / node.scale.y) * 0.4
			_setup_skeletal_animation(node, velocity)
		96, 97: # Sign
			_setup_skeletal_animation(node, 0.3)
		150: # Candle
			_setup_skeletal_animation(node, 0.3)
			_setup_light(node, Color(1.0, 0.6, 0.2), 3.0, 0.3, 0.7)
		102, 103, 110, 120: # Tent etc
			_setup_skeletal_animation(node, 0.16)
		MODEL_WATERSPOUT:
			_setup_fountain(node)
		MODEL_MERCHANT_ANIMAL01, MODEL_MERCHANT_ANIMAL02:
			_setup_skeletal_animation(node, 0.16)
			_setup_merchant_animal(node)
		MODEL_TYPE.MODEL_HOUSE01, MODEL_TYPE.MODEL_HOUSE02, \
		MODEL_TYPE.MODEL_HOUSE03, MODEL_TYPE.MODEL_HOUSE04, MODEL_TYPE.MODEL_HOUSE05:
			_setup_house(node, type)
		MODEL_TYPE.MODEL_HOUSE_WALL01, MODEL_TYPE.MODEL_HOUSE_WALL02:
			_setup_house(node, type)
		MODEL_STREET_LIGHT:
			_setup_street_light(node)
		20,21,22,23,24,25,26,27: # Grass
			_setup_grass(node)
		MODEL_TYPE.MODEL_FIRE_LIGHT01:
			_setup_fire(node, [Vector3(0, 2.0, 0)], 0)
		MODEL_TYPE.MODEL_FIRE_LIGHT02:
			_setup_fire(node, [Vector3(-0.3, 0.6, 0)], 0)
		MODEL_TYPE.MODEL_BONFIRE:
			_setup_fire(node, [Vector3(0, 0.6, 0)], 1)
			_setup_house(node, type, true)
		MODEL_TYPE.MODEL_LIGHT01, MODEL_TYPE.MODEL_LIGHT02, MODEL_TYPE.MODEL_LIGHT03:
			var ftype = 0 if type == MODEL_TYPE.MODEL_LIGHT01 else (1 if type == MODEL_TYPE.MODEL_LIGHT02 else 2)
			_setup_fire(node, [Vector3.ZERO], ftype)
			_hide_meshes(node)
		MODEL_TYPE.MODEL_BRIDGE, MODEL_TYPE.MODEL_BRIDGE_STONE:
			_setup_fire(node, [Vector3(-2.0, 0.3, 0.9), Vector3(2.0, 0.3, 0.9)], 0)
		MODEL_TYPE.MODEL_DUNGEON_GATE:
			_setup_fire(node, [Vector3(-1.5, 1.4, -1.5), Vector3(-1.5, 1.4, 1.5)], 0)
		MODEL_TYPE.MODEL_BIRD01:
			_setup_skeletal_animation(node, 0.16)

static func _setup_fire(parent: Node3D, offsets: Array[Vector3], fire_type: int):
	if MU_FIRE_SCRIPT:
		for offset in offsets:
			var fire_pos = parent.global_transform.origin + parent.global_transform.basis * offset
			MU_FIRE_SCRIPT.create(parent, fire_pos, fire_type)

static func _attach_fire_to_bone(skeleton: Skeleton3D, bone_idx: int, fire_type: int):
	if not MU_FIRE_SCRIPT or bone_idx < 0 or bone_idx >= skeleton.get_bone_count(): return
	var attachment = BoneAttachment3D.new()
	attachment.bone_name = skeleton.get_bone_name(bone_idx)
	skeleton.add_child(attachment)
	var offset = Vector3(0, 0.2, 0) if fire_type >= 1 else Vector3.ZERO
	MU_FIRE_SCRIPT.create(attachment, offset, fire_type, true)

static func _attach_light_sprite_to_bone(skeleton: Skeleton3D, bone_idx: int, color: Color, scale: float):
	if bone_idx < 0 or bone_idx >= skeleton.get_bone_count(): return
	var attachment = BoneAttachment3D.new()
	attachment.bone_name = skeleton.get_bone_name(bone_idx)
	skeleton.add_child(attachment)
	var sprite = Sprite3D.new()
	sprite.name = "LightSprite"
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.modulate = color
	sprite.scale = Vector3(scale, scale, scale)
	const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")
	var tex_path = "res://reference/MuMain/src/bin/Data/Object1/light.OZT"
	var tex = MUTextureLoader.load_mu_texture(tex_path)
	sprite.texture = tex
	var mat = StandardMaterial3D.new()
	mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_texture = tex
	mat.albedo_color = color
	sprite.material_override = mat
	attachment.add_child(sprite)

static func _collect_mesh_children(parent: Node3D) -> Array:
	var result = []
	for child in parent.get_children():
		if child is MeshInstance3D:
			result.append(child)
		elif child.get_child_count() > 0:
			result.append_array(_collect_mesh_children(child))
	return result

static func _hide_meshes(parent: Node3D):
	for child in parent.get_children():
		if child is MeshInstance3D:
			child.visible = false
		elif child.get_child_count() > 0:
			_hide_meshes(child)

static func _setup_grass(node: Node3D):
	var variation = randf_range(0.8, 1.2)
	node.scale *= 0.5 * variation

static func _apply_object_alpha(node: Node, alpha: float):
	for child in node.get_children():
		if child is MeshInstance3D:
			for i in range(child.get_surface_override_material_count()):
				var mat = child.get_surface_override_material(i)
				if mat is ShaderMaterial:
					mat.set_shader_parameter("global_alpha", alpha)
				elif mat is StandardMaterial3D:
					if alpha < 1.0:
						mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
						mat.albedo_color.a = alpha
		elif child.get_child_count() > 0:
			_apply_object_alpha(child, alpha)

static func _setup_fountain(node: Node3D, scroll_speed: float = 0.5):
	var meshes = _collect_mesh_children(node)
	var water_mesh: MeshInstance3D = null
	for m in meshes:
		if m.get_meta("mu_mesh_index", -1) == 3:
			water_mesh = m
			break
	
	if water_mesh:
		var mat = water_mesh.get_active_material(0)
		if mat is ShaderMaterial:
			mat.set_shader_parameter("scroll_speed_v", scroll_speed)
			mat.set_shader_parameter("use_sawtooth", true)
			mat.set_shader_parameter("use_bright", true)
			MUMaterialHelper.ensure_correct_shader(mat)
			
	var skeleton = node.find_child("Skeleton", true)
	if skeleton and skeleton is Skeleton3D:
		_attach_fire_to_bone(skeleton, 1, 1)
		_attach_fire_to_bone(skeleton, 4, 1)

static func _setup_merchant_animal(node: Node3D):
	var skeleton = node.find_child("Skeleton", true)
	if skeleton and skeleton is Skeleton3D:
		var light_color = Color(1.0, 0.5, 0.2) 
		_attach_light_sprite_to_bone(skeleton, 48, light_color, 2.5)
		_attach_light_sprite_to_bone(skeleton, 57, light_color, 2.5)
	else:
		_setup_light(node, Color(1.0, 0.4, 0.2), 2.0)

static func _setup_street_light(node: Node3D):
	_setup_light(node, Color(1.0, 0.8, 0.6), 5.0, 0.6, 0.8)
	var skeleton = node.find_child("Skeleton", true)
	if skeleton and skeleton is Skeleton3D:
		_attach_fire_to_bone(skeleton, 1, 1)
		_attach_fire_to_bone(skeleton, 4, 1)

static func _setup_house(container: Node3D, type: int, force_flicker: bool = false):
	var path = container.get_meta("mu_path", "")
	var blend_idx = MUModelRegistry.get_blend_mesh_index(path)
	
	var flicker = force_flicker or type == MODEL_TYPE.MODEL_HOUSE03 or type == MODEL_TYPE.MODEL_HOUSE_WALL02
	var scroll = type == MODEL_TYPE.MODEL_HOUSE04 or type == MODEL_TYPE.MODEL_HOUSE05
	var scroll_v = 0.5
	
	var meshes = _collect_mesh_children(container)
	for child in meshes:
		var surface_idx = child.get_meta("mu_mesh_index", -1)
		if surface_idx == blend_idx and blend_idx != -1:
			if flicker:
				var flicker_node = Node.new()
				flicker_node.set_script(LIGHT_FLICKER_SCRIPT)
				flicker_node.set("target_material", child.get_active_material(0))
				child.add_child(flicker_node)

			if scroll:
				var scroller = Node.new()
				scroller.set_script(UV_SCROLLER_SCRIPT)
				scroller.set("speed", Vector2(0, scroll_v))
				child.add_child(scroller)
	
	if flicker:
		# SVEN: House window (rand()%4+4)*0.1 = 0.4-0.7, Bonfire (rand()%6+4)*0.1 = 0.4-1.0
		var fl_max = 1.0 if force_flicker else 0.7
		_setup_light(container, Color(1.0, 0.6, 0.2), 2.0, 0.4, fl_max, 30.0)

static func _setup_light(node: Node3D, color: Color, energy: float,
		min_e: float = 0.5, max_e: float = 1.2, flicker_spd: float = 10.0):
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
		flicker.set("min_energy", min_e)
		flicker.set("max_energy", max_e)
		flicker.set("flicker_speed", flicker_spd)
		light.add_child(flicker)

static func _setup_skeletal_animation(node: Node3D, velocity: float):
	# Skip if animation is already managed (main.gd creates AnimationController)
	var existing_ctrl = node.find_child("AnimationController", true)
	if existing_ctrl:
		return
	var anim_player = node.find_child("AnimationPlayer", true)
	if anim_player and anim_player is AnimationPlayer:
		var animations = anim_player.get_animation_list()
		if not animations.is_empty():
			anim_player.play(animations[0])
			anim_player.playback_speed = velocity
			var anim = anim_player.get_animation(animations[0])
			anim.loop_mode = Animation.LOOP_LINEAR

static func _apply_blend_mesh(node: Node3D, blend_idx: int):
	for child in node.get_children():
		if child is MeshInstance3D:
			var mesh_idx = child.get_meta("mu_mesh_index", -1)
			if mesh_idx == blend_idx:
				var mat = child.get_active_material(0)
				if mat is ShaderMaterial:
					mat.set_shader_parameter("use_bright", true)
					MUMaterialHelper.ensure_correct_shader(mat)
				elif mat is StandardMaterial3D:
					mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
					mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		elif child is Skeleton3D:
			_apply_blend_mesh(child, blend_idx)
