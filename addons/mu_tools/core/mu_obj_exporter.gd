@tool
extends Node

class_name MUOBJExporter

const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")
const MUTextureResolver = preload("res://addons/mu_tools/util/mu_texture_resolver.gd")
const MUTextureLoader = preload("res://addons/mu_tools/core/mu_texture_loader.gd")

static var _registry = null
static func get_registry():
	if not _registry:
		_registry = load("res://addons/mu_tools/core/mu_model_registry.gd")
	return _registry

## Exports a parsed BMD to OBJ/MTL files
static func export_bmd(parser: Object, bmd_path: String, output_dir: String) -> bool:
	var base_name = bmd_path.get_file().get_basename()
	var obj_path = output_dir.path_join(base_name + ".obj")
	var mtl_path = output_dir.path_join(base_name + ".mtl")

	var obj_file = FileAccess.open(obj_path, FileAccess.WRITE)
	var mtl_file = FileAccess.open(mtl_path, FileAccess.WRITE)

	if not obj_file or not mtl_file:
		return false

	print("[OBJ Exporter] Exporting %s..." % base_name)

	obj_file.store_line("# MU Online OBJ Export")
	obj_file.store_line("mtllib %s" % (base_name + ".mtl"))

	# ── Resolve Bind Pose ──
	var forced_action = get_registry().get_bind_pose_action(bmd_path)
	var action_to_use = null
	var use_ref_pose = false

	var model_name = bmd_path.get_file().get_basename()
	var metadata = get_registry().get_metadata(model_name)
	if metadata.has("use_reference_pose") and metadata["use_reference_pose"]:
		use_ref_pose = true
		print("  [OBJ Exporter] Using Reference Pose (Registry Override)")
	elif forced_action != -1 and parser.actions.size() > forced_action:
		action_to_use = parser.actions[forced_action]
		print("  [OBJ Exporter] Using Forced Bind Pose (Action %d)" % forced_action)
	else:
		if parser.actions.size() > 0:
			action_to_use = parser.actions[0]
			if parser.actions.size() > 1:
				var keys0 = parser.actions[0].keys
				var keys1 = parser.actions[1].keys
				if keys0.size() > 0 and keys1.size() > 0 \
						and keys0[0] != null and keys1[0] != null \
						and not keys0[0].is_empty() and not keys1[0].is_empty():
					var p0 = keys0[0][0].position
					var p1 = keys1[0][0].position
					if p1.length() < p0.length() - 10.0 \
							or (p0.length() > 50.0 and p1.length() < 10.0):
						action_to_use = parser.actions[1]
		if action_to_use:
			var act_idx = parser.actions.find(action_to_use)
			print("  [OBJ Exporter] Using Heuristic Bind Pose (Action %d)" % act_idx)

	# Fallback: if action 0 is zeroed but skeleton has data, use reference pose
	if not use_ref_pose and action_to_use != null and parser.actions.find(action_to_use) == 0:
		var has_any_action_pos = false
		for k in action_to_use.keys:
			if k != null and not k.is_empty() and k[0].position.length() > 0.01:
				has_any_action_pos = true
				break
		var has_any_bone_pos = false
		for b in parser.bones:
			if b.position.length() > 0.01:
				has_any_bone_pos = true
				break
		if has_any_bone_pos and not has_any_action_pos:
			print("  [OBJ Exporter] Action 0 zeroed, falling back to Reference Pose")
			use_ref_pose = true
			action_to_use = null

	# ── Build global bone transforms ──
	var bone_transforms: Array[Transform3D] = []
	bone_transforms.resize(parser.bones.size())
	for i in range(parser.bones.size()):
		var bone = parser.bones[i]
		var pos = bone.position
		var rot = bone.rotation

		var has_keys = action_to_use and action_to_use.keys.size() > i
		if has_keys and action_to_use.keys[i] != null and not action_to_use.keys[i].is_empty():
			var key0 = action_to_use.keys[i][0]
			pos = key0.position
			rot = key0.rotation

		var local_transform = MUTransformPipeline.build_local_transform(pos, rot)

		if bone.parent_index >= 0 and bone.parent_index < i:
			bone_transforms[i] = bone_transforms[bone.parent_index] * local_transform
		else:
			bone_transforms[i] = local_transform

	# ── Write geometry ──
	var v_offset = 1
	var vt_offset = 1
	var vn_offset = 1
	var materials = []

	for m_idx in range(parser.meshes.size()):
		var mesh = parser.meshes[m_idx]

		# Skip effect meshes (V=0) — no geometry to export
		if mesh.vertex_count == 0:
			continue

		obj_file.store_line("o Mesh_%d" % m_idx)

		var mat_name = mesh.texture_filename.get_basename()
		obj_file.store_line("usemtl %s" % mat_name)

		if not mat_name in materials:
			materials.append(mat_name)

			# Resolve, convert, and save texture as PNG
			var source_tex = MUTextureResolver.resolve_texture_path(bmd_path, mesh.texture_filename)
			if not source_tex.is_empty():
				var tex = MUTextureLoader.load_mu_texture(source_tex)
				if tex:
					var img = tex.get_image()

					var tweak = get_registry().get_texture_tweak(mesh.texture_filename)
					if not tweak.is_empty():
						if tweak.get("flip_x", false): img.flip_x()
						if tweak.get("flip_y", false): img.flip_y()
						if tweak.get("colorkey", false):
							img.convert(Image.FORMAT_RGBA8)
							for y in range(img.get_height()):
								for x in range(img.get_width()):
									var c = img.get_pixel(x, y)
									if c.r < 0.05 and c.g < 0.05 and c.b < 0.05:
										c.a = 0.0
										img.set_pixel(x, y, c)

					var target_png = output_dir.path_join(mat_name + ".png")
					if img.save_png(target_png) == OK:
						print("  [OBJ Exporter] Texture: %s.png" % mat_name)
					else:
						push_warning("[OBJ Exporter] PNG save failed: %s" % target_png)
				else:
					push_warning("[OBJ Exporter] Texture load failed: %s" % source_tex)
			else:
				push_warning("[OBJ Exporter] Texture not found: %s" % mesh.texture_filename)

			_write_mtl_entry(mtl_file, mat_name, mesh.texture_filename)

		# Vertices
		for v_idx in range(mesh.vertices.size()):
			var bone_idx = mesh.vertex_nodes[v_idx]
			var pos_godot = MUTransformPipeline.local_mu_to_godot(mesh.vertices[v_idx])

			if bone_idx >= 0 and bone_idx < bone_transforms.size():
				pos_godot = bone_transforms[bone_idx] * pos_godot

			var rot_override = get_registry().get_rotation_override(bmd_path)
			if rot_override != Vector3.ZERO:
				var rot_basis = Basis.from_euler(rot_override * (PI / 180.0))
				pos_godot = rot_basis * pos_godot

			if not pos_godot.is_finite():
				push_warning("[OBJ Exporter] Non-finite vertex in Mesh_%d idx %d" % [m_idx, v_idx])
				pos_godot = Vector3.ZERO

			obj_file.store_line("v %.6f %.6f %.6f" % [pos_godot.x, pos_godot.y, pos_godot.z])

		# UVs
		for uv in mesh.uv_coords:
			obj_file.store_line("vt %.6f %.6f" % [uv.x, 1.0 - uv.y])

		# Normals
		for n_idx in range(mesh.normals.size()):
			var norm_mu = mesh.normals[n_idx]
			var bone_idx = mesh.normal_nodes[n_idx]

			var basis = Basis.IDENTITY
			if bone_idx >= 0 and bone_idx < bone_transforms.size():
				basis = bone_transforms[bone_idx].basis

			var n_godot = MUTransformPipeline.mu_normal_to_godot(norm_mu, basis)

			var rot_override = get_registry().get_rotation_override(bmd_path)
			if rot_override != Vector3.ZERO:
				var rot_basis = Basis.from_euler(rot_override * (PI / 180.0))
				n_godot = rot_basis * n_godot

			obj_file.store_line("vn %.6f %.6f %.6f" % [n_godot.x, n_godot.y, n_godot.z])

		# Faces — MU CW winding + X-negate in local_mu_to_godot = CCW.
		# OBJ expects CCW, so indices stay [0, 1, 2].
		for tri in mesh.triangles:
			var f = "f"
			for i in [0, 1, 2]:
				var vi = tri.vertex_indices[i] + v_offset
				var ti = tri.uv_indices[i] + vt_offset
				var ni = tri.normal_indices[i] + vn_offset
				f += " %d/%d/%d" % [vi, ti, ni]
			obj_file.store_line(f)

		v_offset += mesh.vertices.size()
		vt_offset += mesh.uv_coords.size()
		vn_offset += mesh.normals.size()

	obj_file.close()
	mtl_file.close()
	return true

## Write MTL entry using centralized keyword detection
static func _write_mtl_entry(file: FileAccess, mat_name: String, tex_name: String):
	file.store_line("newmtl %s" % mat_name)
	file.store_line("map_Kd %s" % (tex_name.get_basename() + ".png"))

	var low_tex = tex_name.to_lower()

	# Use centralized keywords (same as MUMaterialHelper / MUModelRegistry)
	var is_effect = false
	for kw in ["light", "fire", "effect", "water", "spout"]:
		if kw in low_tex:
			is_effect = true
			break

	var is_shadow = false
	if "shadow" in low_tex:
		is_shadow = true

	var is_cutout = false
	for kw in ["tree", "grass", "leaf", "fence", "gate", "barred", "straw", "bamboo"]:
		if kw in low_tex:
			is_cutout = true
			break

	if is_shadow:
		file.store_line("d 0.7")
		file.store_line("map_d %s" % (tex_name.get_basename() + ".png"))
	elif is_effect:
		file.store_line("d 0.5")
		file.store_line("map_d %s" % (tex_name.get_basename() + ".png"))
	elif is_cutout:
		file.store_line("map_d %s" % (tex_name.get_basename() + ".png"))

	file.store_line("Ka 1.0 1.0 1.0")
	file.store_line("Kd 1.0 1.0 1.0")
	file.store_line("Ks 0.0 0.0 0.0")
	file.store_line("Ns 10.0")
	file.store_line("illum 1")
