@tool
extends Node

class_name MUOBJExporter

const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")
# const MUModelRegistry = preload("res://addons/mu_tools/core/mu_model_registry.gd") # Dynamic load
const MUTextureResolver = preload("res://addons/mu_tools/util/mu_texture_resolver.gd")

static var _Registry = null
static func get_registry():
	if not _Registry:
		_Registry = load("res://addons/mu_tools/core/mu_model_registry.gd")
	return _Registry
const MUTextureLoader = preload("res://addons/mu_tools/core/mu_texture_loader.gd")

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
	
	# Write OBJ Header
	obj_file.store_line("# MU Online OBJ Export (Pipeline 2.0)")
	obj_file.store_line("mtllib %s" % (base_name + ".mtl"))
	
	# Resolve Bind Pose Selection
	# 1. Check Registry for "Forced Action" override
	var forced_action = get_registry().get_bind_pose_action(bmd_path)
	var action_to_use = null
	
	# 2. Check Registry for "Reference Pose" override
	# Some assets (like MerchantAnimals or NPCs) explode if we use Action 0.
	# This override forces using the Skeleton Header bind pose.
	var use_ref_pose = false
	var model_name = bmd_path.get_file().get_basename()
	var metadata = get_registry().get_metadata(model_name)
	if metadata.has("use_reference_pose") and metadata["use_reference_pose"]:
		use_ref_pose = true
		print("    [OBJ Exporter] Forced Reference Pose (Registry Override)")
	
	# 3. Determine Pose
	if use_ref_pose:
		print("  [OBJ Exporter] Using Skeleton Header Bind Pose (Reference Pose)")
		# action_to_use remains null, which means bone_transforms will be built from bone.position/rotation directly
	elif forced_action != -1 and parser.actions.size() > forced_action:
		action_to_use = parser.actions[forced_action]
		print("  [OBJ Exporter] Using Forced Bind Pose (Action %d)" % forced_action)
	else:
		# Fallback to Origin-Proximity Heuristic
		if parser.actions.size() > 0:
			action_to_use = parser.actions[0]
			if parser.actions.size() > 1:
				var keys0 = parser.actions[0].keys
				var keys1 = parser.actions[1].keys
				if keys0.size() > 0 and keys1.size() > 0 and not keys0[0].is_empty() and not keys1[0].is_empty():
					var p0 = keys0[0][0].position
					var p1 = keys1[0][0].position
					if p1.length() < p0.length() - 10.0 or (p0.length() > 50.0 and p1.length() < 10.0):
						action_to_use = parser.actions[1]
		if action_to_use:
			print("  [OBJ Exporter] Using Heuristic Bind Pose (Action %d)" % parser.actions.find(action_to_use))

	var bone_transforms: Array[Transform3D] = []
	bone_transforms.resize(parser.bones.size())
	for i in range(parser.bones.size()):
		var bone = parser.bones[i]
		var pos = bone.position # Centimeters
		var rot = bone.rotation # Radians
		
		# Overlay action data if available
		if action_to_use and action_to_use.keys.size() > i and action_to_use.keys[i] != null and not action_to_use.keys[i].is_empty():
			var key0 = action_to_use.keys[i][0]
			pos = key0.position
			rot = key0.rotation
		
		# Build Authoritative Godot-space transform
		var local_transform = MUTransformPipeline.build_local_transform(pos, rot)
		
		if bone.parent_index >= 0 and bone.parent_index < i:
			bone_transforms[i] = bone_transforms[bone.parent_index] * local_transform
		else:
			bone_transforms[i] = local_transform

	var v_offset = 1 
	var vt_offset = 1
	var vn_offset = 1
	var materials = []

	for m_idx in range(parser.meshes.size()):
		var mesh = parser.meshes[m_idx]
		obj_file.store_line("o Mesh_%d" % m_idx)
		
		var mat_name = mesh.texture_filename.get_basename()
		obj_file.store_line("usemtl %s" % mat_name)
		if not mat_name in materials:
			materials.append(mat_name)
			
			# ROBUST TEXTURE PIPELINE: Resolve and Convert
			var source_tex = MUTextureResolver.resolve_texture_path(bmd_path, mesh.texture_filename)
			if not source_tex.is_empty():
				var tex = MUTextureLoader.load_mu_texture(source_tex)
				if tex:
					var img = tex.get_image()
					
					# Apply Texture Registry Tweaks
					# Apply Texture Registry Tweaks
					var tweak = get_registry().get_texture_tweak(mesh.texture_filename)
					if not tweak.is_empty():
						if tweak.get("flip_x", false):
							img.flip_x()
							print("    [OBJ Exporter] Applied Tweak: Flip X")
						if tweak.get("flip_y", false):
							img.flip_y()
							print("    [OBJ Exporter] Applied Tweak: Flip Y")
							
						if tweak.get("colorkey", false):
							# Convert Black (0,0,0) to Transparent
							print("    [OBJ Exporter] Applied Tweak: Colorkey (Black->Alpha)")
							img.convert(Image.FORMAT_RGBA8)
							var w = img.get_width()
							var h = img.get_height()
							for y in range(h):
								for x in range(w):
									var c = img.get_pixel(x, y)
									# Threshold for black (compression artifacts)
									if c.r < 0.05 and c.g < 0.05 and c.b < 0.05:
										c.a = 0.0
										img.set_pixel(x, y, c)
							
					var target_png = output_dir.path_join(mat_name + ".png")
					if img.save_png(target_png) == OK:
						print("  [OBJ Exporter] ✓ Converted and Attached: ", mat_name + ".png")
					else:
						print("  [OBJ Exporter] ✗ Failed to save PNG: ", target_png)
				else:
					print("  [OBJ Exporter] ✗ Failed to load/decrypt texture: ", source_tex)
			else:
				print("  [OBJ Exporter] ⚠ Could not resolve source texture for: ", mesh.texture_filename)
				
			_write_mtl_entry(mtl_file, mat_name, mesh.texture_filename, mesh.render_flags)

		# Vertices
		for v_idx in range(mesh.vertices.size()):
			var bone_idx = mesh.vertex_nodes[v_idx]
			
			# CENTRAL TRANSFORMATION (Godot-space Frame)
			var pos_godot = MUTransformPipeline.local_mu_to_godot(mesh.vertices[v_idx])
			
			if bone_idx >= 0 and bone_idx < bone_transforms.size():
				pos_godot = bone_transforms[bone_idx] * pos_godot
			
			# Apply Registry Rotation Override
			var rot_override = get_registry().get_rotation_override(bmd_path)
			if rot_override != Vector3.ZERO:
				var rot_basis = Basis.from_euler(rot_override * (PI / 180.0))
				pos_godot = rot_basis * pos_godot
				
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
			
			# CENTRAL TRANSFORMATION
			var n_godot = MUTransformPipeline.mu_normal_to_godot(norm_mu, basis)
			
			# Apply Registry Rotation Override
			var rot_override = get_registry().get_rotation_override(bmd_path)
			if rot_override != Vector3.ZERO:
				var rot_basis = Basis.from_euler(rot_override * (PI / 180.0))
				n_godot = rot_basis * n_godot
				
			obj_file.store_line("vn %.6f %.6f %.6f" % [n_godot.x, n_godot.y, n_godot.z])
			
		# Faces
		for tri in mesh.triangles:
			var f = "f"
			for i in [0, 1, 2]: # Mirrored CCW Winding mapping
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

static func _write_mtl_entry(file: FileAccess, mat_name: String, tex_name: String, render_flags: int = 0):
	file.store_line("newmtl %s" % mat_name)
	file.store_line("map_Kd %s" % (tex_name.get_basename() + ".png"))
	
	var low_tex = tex_name.to_lower()
	var is_transparent = render_flags > 0 or \
		low_tex.contains("leaf") or \
		low_tex.contains("tree") or \
		low_tex.contains("_a") or \
		low_tex.contains("shadow") or \
		low_tex.contains("grass")
		
	if is_transparent:
		# If it's a shadow, use lower opacity to avoid "Black Mask" look
		if low_tex.contains("shadow"):
			file.store_line("d 0.7") 
			file.store_line("map_d %s" % (tex_name.get_basename() + ".png"))
		else:
			# For non-shadows (like trees), do NOT enforce generic opacity.
			# Trust the image alpha or map_d.
			pass # No 'd' value means default 1.0
			file.store_line("map_d %s" % (tex_name.get_basename() + ".png"))
	
	file.store_line("Ka 1.0 1.0 1.0")
	file.store_line("Kd 1.0 1.0 1.0")
	file.store_line("Ks 0.0 0.0 0.0")
	file.store_line("Ns 10.0")
	file.store_line("illum 1")
