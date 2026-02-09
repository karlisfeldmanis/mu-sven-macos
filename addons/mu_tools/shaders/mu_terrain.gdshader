shader_type spatial;
render_mode cull_disabled, unshaded;

uniform sampler2DArray tile_textures : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D terrain_data_lut : filter_nearest; 
uniform sampler2D layer1_map : filter_nearest;
uniform sampler2D layer2_map : filter_nearest;
uniform sampler2D alpha_map : filter_linear;
uniform sampler2D lightmap_map : filter_linear;
uniform sampler2D grass_offset_map : filter_nearest;
uniform sampler2D water_map : filter_nearest;
uniform sampler2D shadow_map : source_color, filter_linear, repeat_disable;
uniform int debug_mode : hint_range(0, 10) = 0;
uniform int highlight_index : hint_range(-1, 255) = -1;
global uniform float mu_world_time;
global uniform float mu_water_move_uv;

varying vec3 v_world_pos;
varying vec3 v_color;
varying vec3 v_normal;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_color = COLOR.rgb;
	v_normal = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;
}

uniform sampler2D symmetry_tile_map : filter_nearest;
uniform int debug_view = 0; // Default: Normal Render

// 🟢 SVEN D4 Tile Symmetry Logic
// orientation: 0-7 (from attribute high byte)
// scale: tile tiling factor
vec2 apply_tile_symmetry(vec2 uv_fract, int orientation) {
	vec2 res = uv_fract;
	// SVEN Parity: D4 Group orientations
	if (orientation == 1) res.x = 1.0 - res.x;
	else if (orientation == 2) res.y = 1.0 - res.y;
	else if (orientation == 3) res = vec2(1.0) - res;
	else if (orientation >= 4) {
		res = vec2(res.y, 1.0 - res.x); // 90 CW
		int sub = orientation - 4;
		if (sub == 1) res.x = 1.0 - res.x;
		else if (sub == 2) res.y = 1.0 - res.y;
		else if (sub == 3) res = vec2(1.0) - res;
	}
	return res;
}

void fragment() {
	// 🟢 Absolute SVEN Stable Sync (Coordinate Indexing)
	// Standard World-Space Mapping (X=U, Z=V)
	vec2 base_uv = floor(v_world_pos.xz + 0.001); 
	
	// 🟢 Absolute Pivot-256 Mapping Sync (Transposed Parity)
	// Godot X [0, 256] mirrors to MU Row [0, 256] at pivot 256.
	// Godot Z [0, 256] maps to MU Column [0, 256].
	vec2 mu_coords = vec2(floor(v_world_pos.z), floor(256.0 - v_world_pos.x - 0.001));
	vec2 map_uv_stable = (mu_coords + 0.5) / 256.0;
	
	// Smooth sample for blend mask (Transposed)
	vec2 map_uv_smooth = vec2(v_world_pos.z, 256.0 - v_world_pos.x) / 256.0;
	
	float h_idx1 = floor(texture(layer1_map, map_uv_stable).r * 255.0 + 0.1);
	float h_idx2 = floor(texture(layer2_map, map_uv_stable).r * 255.0 + 0.1);
	int tile_sym = int(texture(symmetry_tile_map, map_uv_stable).r * 255.0 + 0.1);
	
	// Data LUT (Data is always 256x1, no mesh dependency)
	vec4 d1 = texture(terrain_data_lut, vec2((h_idx1 + 0.5) / 256.0, 0.5));
	vec4 d2 = texture(terrain_data_lut, vec2((h_idx2 + 0.5) / 256.0, 0.5));
	float s1 = d1.r; float sym1 = d1.g; int cat1 = int(d1.b + 0.1);
	float s2 = d2.r; float sym2 = d2.g; int cat2 = int(d2.b + 0.1);

	vec2 dX = dFdx(v_world_pos.xz);
	vec2 dY = dFdy(v_world_pos.xz);

	float a_val = texture(alpha_map, map_uv_smooth).r;
	vec2 uv_base = vec2(v_world_pos.z, 256.0 - v_world_pos.x);
	vec2 uv_fract = fract(uv_base);
	vec2 uv_floor = floor(uv_base);
	
	// 🟢 Layer 1 UV (Transposed + Per-Tile Symmetry)
	vec2 uv1_fract = apply_tile_symmetry(uv_fract, tile_sym);
	vec2 uv1 = (uv_floor + uv1_fract) * s1;
	float real_idx1 = h_idx1;
	
	vec3 tex1 = texture(tile_textures, vec3(uv1, real_idx1)).rgb;
	
	// 🟢 Layer 2 UV
	vec3 tex2 = tex1;
	float real_idx2 = h_idx2;
	if (h_idx2 < 254.1) {
		vec2 uv2_fract = apply_tile_symmetry(uv_fract, tile_sym);
		vec2 uv2 = (uv_floor + uv2_fract) * s2;
		tex2 = texture(tile_textures, vec3(uv2, real_idx2)).rgb;
	}
	
	vec3 lightmap_color = texture(lightmap_map, map_uv_smooth).rgb;
	vec3 final_color = mix(tex1, tex2, a_val);

	// SVEN Parity: High-Visibility Base Lighting
	// The lightmap is often dark in MU assets and needs a significant boost in Godot.
	vec3 light_dir = normalize(vec3(0.5, 1.0, 0.5)); 
	float diff = dot(normalize(v_normal), light_dir);
	
	// Ultra-Bright Sven Lighting (Fixed High Ambient)
	float luminosity = clamp(diff * 0.2 + 1.5, 1.2, 2.0);
	vec3 light_v = max(lightmap_color * 1.5, vec3(0.6)) * luminosity;

	// Water
	if (cat1 == 2) {
		vec2 w_uv_fract = apply_tile_symmetry(uv_fract, tile_sym);
		vec2 w_uv = (uv_floor + w_uv_fract) * 2.0;
		w_uv.x += mu_world_time * 0.1;
		w_uv.y += sin(mu_world_time * 2.0 + v_world_pos.z * 0.1) * 0.02;
		final_color = textureGrad(tile_textures, vec3(w_uv, h_idx1), dX, dY).rgb;
	}

	if (debug_view != 0) {
        // Debug views removed
    }
	ALBEDO = final_color * light_v;
	// Gamma Correction (SVEN is vibrant)
	ALBEDO = pow(ALBEDO, vec3(0.8)); 
}
