shader_type spatial;
render_mode cull_disabled, unshaded;

uniform sampler2DArray tile_textures : source_color, filter_nearest, repeat_enable;
uniform sampler2D terrain_data_lut : filter_nearest;
uniform sampler2D layer1_map : filter_nearest;
uniform sampler2D layer2_map : filter_nearest;
uniform sampler2D alpha_map : filter_linear;
uniform sampler2D lightmap_map : filter_linear;
uniform sampler2D symmetry_map : filter_nearest;
uniform int debug_view = 0; // 0: Normal, 1: Layer1 Index, 6: Alpha, 7: Lightmap
global uniform float mu_world_time;
global uniform float mu_water_move_uv;
global uniform float mu_wind_speed;

varying vec3 v_world_pos;
varying vec3 v_color;
varying vec3 v_normal;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_color = COLOR.rgb;
	v_normal = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;
}

// SVEN ATT symmetry: transform tile-local UV using bit flags
// Bit 0 (1): Flip X, Bit 1 (2): Flip Y, Bit 2 (4): Rotate 90 CW
vec2 apply_symmetry(vec2 tile_local, int sym) {
	vec2 r = tile_local;
	if ((sym & 1) != 0) r.x = 1.0 - r.x;
	if ((sym & 2) != 0) r.y = 1.0 - r.y;
	if ((sym & 4) != 0) {
		float tmp = r.x;
		r.x = 1.0 - r.y;
		r.y = tmp;
	}
	return r;
}

void fragment() {
	// UV mapped 0-1 across the 256x256 grid.
	vec2 uv_base = UV * 256.0;
	vec2 uv_floor = floor(uv_base + 0.0001);

	// Stable sampling coords for 256x256 maps
	vec2 map_uv_stable = (uv_floor + 0.5) / 256.0;
	// Smooth sampling for alpha/lightmaps
	vec2 map_uv_smooth = UV;

	// 1. Sample Layer Indices
	float h_idx1 = floor(texture(layer1_map, map_uv_stable).r * 255.0 + 0.1);
	float h_idx2 = floor(texture(layer2_map, map_uv_stable).r * 255.0 + 0.1);

	// 2. Sample Data LUT (R = scale factor = 64.0 / tex_width, G = unused, B = category)
	vec4 d1 = texture(terrain_data_lut, vec2((h_idx1 + 0.5) / 256.0, 0.5));
	vec4 d2 = texture(terrain_data_lut, vec2((h_idx2 + 0.5) / 256.0, 0.5));
	float s1 = d1.r;
	int cat1 = int(d1.b + 0.1);
	float s2 = d2.r;
	int cat2 = int(d2.b + 0.1);

	// 3. Bilinear Alpha Mask Sampling (SVEN VertexAlpha0-3 parity)
	vec2 a_uv = uv_base - 0.5;
	vec2 a_uv_fract = fract(a_uv);
	vec2 a_uv_floor = floor(a_uv);
	float a00 = texture(alpha_map, (a_uv_floor + vec2(0.5, 0.5)) / 256.0).r;
	float a10 = texture(alpha_map, (a_uv_floor + vec2(1.5, 0.5)) / 256.0).r;
	float a01 = texture(alpha_map, (a_uv_floor + vec2(0.5, 1.5)) / 256.0).r;
	float a11 = texture(alpha_map, (a_uv_floor + vec2(1.5, 1.5)) / 256.0).r;
	float a_val = mix(mix(a00, a10, a_uv_fract.x), mix(a01, a11, a_uv_fract.x), a_uv_fract.y);

	// 4. Per-tile symmetry from ATT data (SVEN flip/rotate for road textures)
	float sym_raw = texture(symmetry_map, map_uv_stable).r * 255.0;
	int sym = int(sym_raw + 0.5);

	// Compute symmetry-transformed UV for discrete textures (cat 0 = roads)
	vec2 tile_local = fract(uv_base);
	vec2 sym_local = apply_symmetry(tile_local, sym);
	vec2 uv_sym = floor(uv_base) + sym_local;

	// 5. Layer 1 UV — C++ Parity: uv = tile_pos * (64.0 / tex_width)
	// s1 is pre-computed as 64.0 / tex_width in the LUT.
	vec2 dX = dFdx(uv_base);
	vec2 dY = dFdy(uv_base);

	// Roads (cat 0) use per-tile symmetry, others use raw UV
	vec2 uv1_base = (cat1 == 0) ? uv_sym : uv_base;
	vec2 uv1 = uv1_base * s1;
	vec2 dX1 = dX * s1;
	vec2 dY1 = dY * s1;

	// Water: C++ adds WaterMove to U, TerrainGrassWind wave to V
	if (cat1 == 2) {
		uv1.x += mu_water_move_uv;
		// SVEN: TerrainGrassWind = sin(WindSpeed + xf*5) * 10; svf += wind * 0.002
		float grass_wind1 = sin(mu_wind_speed + uv_base.x * 5.0) * 10.0;
		uv1.y += grass_wind1 * 0.002;
	}
	vec3 tex1 = textureGrad(tile_textures, vec3(uv1, h_idx1), dX1, dY1).rgb;

	// 6. Layer 2 UV — Same logic with symmetry
	vec3 tex2 = tex1;
	if (h_idx2 < 254.1) {
		vec2 uv2_base = (cat2 == 0) ? uv_sym : uv_base;
		vec2 uv2 = uv2_base * s2;
		vec2 dX2 = dX * s2;
		vec2 dY2 = dY * s2;

		if (cat2 == 2) {
			uv2.x += mu_water_move_uv;
			float grass_wind2 = sin(mu_wind_speed + uv_base.x * 5.0) * 10.0;
			uv2.y += grass_wind2 * 0.002;
		}
		tex2 = textureGrad(tile_textures, vec3(uv2, h_idx2), dX2, dY2).rgb;
	}

	// 7. SVEN Advanced Lighting Parity
	// Formula: FinalLight = Lightmap * (Dot(Normal, LightDir) + 0.5)
	// SVEN LightDir is (0.5, -0.5, 0.5) in MU coords.
	// In Godot (Y-up), this maps roughly to (0.5, 0.5, 0.5) or tailored to scene light.
	vec3 lightmap_color = texture(lightmap_map, map_uv_smooth).rgb;
	vec3 sven_light_dir = normalize(vec3(0.5, 1.0, 0.5)); 
	float diff = dot(normalize(v_normal), sven_light_dir);
	
	// SVEN uses a clamp/lerp for luminosity: (diff * 0.15 + 1.0)
	float luminosity = clamp(diff * 0.15 + 1.0, 0.8, 1.3);
	
	// BackTerrainLight = TerrainLight * Luminosity
	vec3 final_light = lightmap_color * luminosity;

	// Global intensity adjustment for Godot visibility
	final_light = clamp(final_light * 1.2, vec3(0.4), vec3(1.8));

	// 8. Final Blending & Glow (SVEN RenderFaceAlpha vs RenderFaceBlend)
	// Base layer is always modulated by lightmap.
	vec3 base_color = tex1 * final_light;
	vec3 final_color;
	
	if (cat2 == 2) {
		// Glow/Water Pass (Additive: GL_ONE, GL_ONE)
		// Unshaded by terrain lightmap (fullbright alpha-tinted)
		final_color = base_color + (tex2 * a_val);
	} else {
		// Normal Transition (Interpolated: GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
		// Both layers modulated by lightmap.
		final_color = mix(tex1, tex2, a_val) * final_light;
	}

	// 9. Debug Views
	if (debug_view != 0) {
		if (debug_view == 1) final_color = vec3(h_idx1 / 255.0);
		if (debug_view == 6) final_color = vec3(a_val);
		if (debug_view == 7) final_color = lightmap_color;
		if (debug_view == 9) final_color = vec3(float(cat1) / 5.0, 0.0, 1.0 - float(cat1) / 5.0);
		if (debug_view == 10) final_color = vec3(sym_raw / 7.0, 0.0, 0.0);
	}
	
	ALBEDO = final_color;
}
