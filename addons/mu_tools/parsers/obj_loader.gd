@tool
extends Node

# class_name MUObjLoader

## Static utility for loading OBJ files at runtime.
## Handles all standard face formats, quad triangulation,
## and automatic texture resolution.

const MUMaterialHelper = preload(
	"res://addons/mu_tools/rendering/material_helper.gd"
)
const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")
const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")

static func load_obj(path: String) -> Dictionary:
	var abs_path = ProjectSettings.globalize_path(path)
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		push_warning("[MUObjLoader] Cannot open: %s" % path)
		return {}

	var mesh = ArrayMesh.new()
	var st = SurfaceTool.new()
	
	var raw_verts := []
	var raw_normals := []
	var raw_uvs := []
	var materials_mapping := [] # Array of material names indexed by surface
	
	var current_mat_name = "default"
	var surface_started = false

	while file.get_position() < file.get_length():
		var line = file.get_line().strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
			
		var parts = line.split(" ", false)
		var cmd = parts[0]

		match cmd:
			"mtllib":
				# Could potentially parse multiple MTLs
				pass
			"v":
				var v = Vector3(
					parts[1].to_float(), parts[2].to_float(), parts[3].to_float()
				)
				raw_verts.append(v)
			"vn":
				var vn = Vector3(
					parts[1].to_float(), parts[2].to_float(), parts[3].to_float()
				)
				raw_normals.append(vn)
			"vt":
				raw_uvs.append(Vector2(parts[1].to_float(), parts[2].to_float()))
			"usemtl":
				var next_mat = parts[1]
				if surface_started:
					mesh = st.commit(mesh)
					materials_mapping.append(current_mat_name)
					st.clear()
				
				current_mat_name = next_mat
				st.begin(Mesh.PRIMITIVE_TRIANGLES)
				surface_started = true
			"f":
				if not surface_started:
					st.begin(Mesh.PRIMITIVE_TRIANGLES)
					surface_started = true
				_parse_face(line, st, raw_verts, raw_normals, raw_uvs)

	if surface_started:
		mesh = st.commit(mesh)
		materials_mapping.append(current_mat_name)

	if raw_verts.is_empty():
		return {}

	print("[MUObjLoader] Parsed %s: %d surfaces, %d verts" % [
		path.get_file(), mesh.get_surface_count(), raw_verts.size()
	])
	
	return {
		"mesh": mesh,
		"materials": materials_mapping
	}


static func _parse_face(
	line: String, st: SurfaceTool,
	verts: Array, normals: Array, uvs: Array
) -> void:
	var parts = line.split(" ", false)
	var face := []
	for i in range(1, parts.size()):
		var indices = parts[i].split("/")
		var v_idx = indices[0].to_int() - 1
		var t_idx = -1
		var n_idx = -1
		if indices.size() > 1 and indices[1] != "":
			t_idx = indices[1].to_int() - 1
		if indices.size() > 2 and indices[2] != "":
			n_idx = indices[2].to_int() - 1
		face.append({"v": v_idx, "vt": t_idx, "vn": n_idx})

	for i in range(1, face.size() - 1):
		for fi in [0, i, i + 1]:
			var fv = face[fi]
			if fv["vn"] >= 0 and fv["vn"] < normals.size():
				st.set_normal(normals[fv["vn"]])
			if fv["vt"] >= 0 and fv["vt"] < uvs.size():
				st.set_uv(uvs[fv["vt"]])
			st.add_vertex(verts[fv["v"]])


## Parses MTL file to resolve actual texture paths.
static func parse_mtl(path: String) -> Dictionary:
	var mtl_data = {}
	if not FileAccess.file_exists(path):
		return mtl_data
		
	var file = FileAccess.open(path, FileAccess.READ)
	var current_mat = ""
	
	while file.get_position() < file.get_length():
		var line = file.get_line().strip_edges()
		if line.is_empty() or line.begins_with("#"): continue
		
		var parts = line.split(" ", false)
		match parts[0]:
			"newmtl":
				current_mat = parts[1]
				var info = {"tex": "", "color": Color.WHITE, "alpha": 1.0}
				mtl_data[current_mat] = info
			"map_Kd":
				if current_mat != "":
					mtl_data[current_mat]["tex"] = parts[1]
			"Kd":
				if current_mat != "" and parts.size() >= 4:
					mtl_data[current_mat]["color"] = Color(parts[1].to_float(), parts[2].to_float(), parts[3].to_float())
			"d":
				if current_mat != "":
					mtl_data[current_mat]["alpha"] = parts[1].to_float()
	return mtl_data


## Full pipeline: parse OBJ -> resolve MTL -> apply proper textures.
static func build_mesh_instance(
	obj_path: String,
	_animated_materials: Array = []
) -> MeshInstance3D:
	var data = load_obj(obj_path)
	if data.is_empty():
		return null

	var mesh = data["mesh"]
	var mat_names = data["materials"]
	
	# Load MTL mapping
	var mtl_path = obj_path.get_base_dir().path_join(obj_path.get_file().get_basename() + ".mtl")
	var mtl_textures = parse_mtl(mtl_path)
	
	var mi = MeshInstance3D.new()
	mi.mesh = mesh
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF

	for i in range(mesh.get_surface_count()):
		var mat_name = mat_names[i]
		var mat_info = mtl_textures.get(mat_name, {"tex": "", "color": Color.WHITE, "alpha": 1.0})
		
		var mat = StandardMaterial3D.new()
		# mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mat.cull_mode = BaseMaterial3D.CULL_DISABLED
		mat.albedo_color = mat_info["color"]
		mat.albedo_color.a = mat_info["alpha"]
		
		if mat_info["alpha"] < 1.0:
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		
		var tex_file = mat_info["tex"]
		if tex_file != "":
			var tex_path = obj_path.get_base_dir().path_join(tex_file)
			
			# 1. Smarter Case-Insensitive Extension Resolution
			var extensions = ["", ".png", ".ozj", ".ozt", ".jpg", ".tga", ".OZJ", ".OZT", ".PNG", ".JPG", ".TGA"]
			var resolved_path = ""
			
			for ext_suffix in extensions:
				var trial_path = tex_path
				if ext_suffix != "":
					trial_path = tex_path.get_basename() + ext_suffix
				
				if ResourceLoader.exists(trial_path):
					resolved_path = trial_path
					mat.albedo_texture = ResourceLoader.load(resolved_path)
					break
				elif tex_path.get_extension().to_upper() in ["OZJ", "OZT", "JPG", "TGA"] or ext_suffix.to_upper() in [".OZJ", ".OZT"]:
					var trial_tex = MUTextureLoader.load_mu_texture(trial_path)
					if trial_tex:
						resolved_path = trial_path
						mat.albedo_texture = trial_tex
						break
				elif FileAccess.file_exists(trial_path):
					var img = Image.load_from_file(ProjectSettings.globalize_path(trial_path))
					if img:
						resolved_path = trial_path
						mat.albedo_texture = ImageTexture.create_from_image(img)
						break
			
			if resolved_path != "":
				MUMaterialHelper.setup_material(mat, 0, obj_path, resolved_path, [])
				# Force Alpha Scissor override for foliage
				var low_tex = resolved_path.to_lower()
				if "grass" in low_tex or "bush" in low_tex or "leaf" in low_tex:
					mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
					mat.alpha_scissor_threshold = 0.05
			else:
				print("[MUObjLoader] FAILED to resolve texture: ", tex_path, " for ", obj_path)
				mat.albedo_color = Color.MAGENTA
		elif mat_info["color"] == Color.WHITE and mat_info["alpha"] == 1.0:
			# If no color and no texture, it's a fallback error
			mat.albedo_color = Color.MAGENTA
			
		mi.set_surface_override_material(i, mat)

	return mi
