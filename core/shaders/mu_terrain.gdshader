shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform sampler2DArray tile_textures : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D layer1_map : filter_nearest; // R8 texture containing tile indices
uniform sampler2D layer2_map : filter_nearest; // R8 texture containing tile indices
uniform sampler2D alpha_map : filter_linear;  // R8 texture containing blend values
uniform sampler2D lightmap_map : filter_linear;
uniform sampler2D grass_offset_map : filter_nearest; // R8 random UV offsets
uniform sampler2D water_map : filter_nearest; // R8 water mask
uniform sampler2D shadow_map : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D scale_map : filter_nearest; // R32F texture containing UV scale factors
uniform int debug_mode : hint_range(0, 10) = 0; // 0: Normal, 1: UVs, 2: L1 Indices, 3: Alpha, 4: Raw UV, 5: Lightmap Only, 6: Coord Grid
uniform int highlight_index : hint_range(-1, 255) = -1;
uniform float lightmap_intensity : hint_range(0.0, 5.0) = 1.0;
uniform float shadow_intensity : hint_range(0.0, 2.0) = 0.5;
uniform float lightmap_softness = 0.5;

global uniform float mu_world_time;
global uniform float mu_water_move_uv;

varying vec3 v_world_pos;
varying float v_slope;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_slope = 1.0 - abs(NORMAL.y); // 0 = flat, 1 = vertical
}

// 🟢 Helper for GL_MIRRORED_REPEAT simulation
vec2 mirror_repeat(vec2 uv) {
	return 1.0 - abs(mod(uv, 2.0) - 1.0);
}

void fragment() {
	if (debug_mode == 4) {
		ALBEDO = vec3(UV, 0.0);
	} else {
		// Calculate high-precision base UV
		vec2 scaled_uv = UV * 255.0; // 255 segments in mesh
		vec2 base_uv = floor(scaled_uv);
		
		// 1. Fetch Indices (Nearest)
		// 🟢 CORRECTED Map Sampling Parity (0.5, 0.5 center)
		vec2 map_uv_nearest = (base_uv + vec2(0.5, 0.5)) / 256.0;
		vec2 map_uv_smooth = (scaled_uv + 0.5) / 256.0;
		
		float idx1 = floor(texture(layer1_map, map_uv_nearest).r * 255.0 + 0.5);
		float idx2 = floor(texture(layer2_map, map_uv_nearest).r * 255.0 + 0.5);
		float alpha_val = texture(alpha_map, map_uv_nearest).r;
		float water_val = texture(water_map, map_uv_nearest).r;

		// 🟢 Authentic Flag Logic: INFERRED from Index & Alpha (Sven Parity)
		// Original "Alpha High Bits" assumption was incorrect.
		// Grass (Flag 2) applies to Indices 0 & 1 when alpha blending < 0.4
		int terrain_flag = 0;
		if (idx1 < 1.5 && alpha_val < 0.4) {
			terrain_flag = 2; // TERRAIN_MAP_GRASS
		}

		float row_offset = 0.0;
		if (terrain_flag == 2) { // TERRAIN_MAP_GRASS
			row_offset = texture(grass_offset_map, vec2(map_uv_nearest.y, 0.5)).r;
		}
		
		// 2. Fetch Scale Factors
		float s1 = texelFetch(scale_map, ivec2(int(idx1), 0), 0).r;
		float s2 = (idx2 > 254.1) ? 1.0 : texelFetch(scale_map, ivec2(int(idx2), 0), 0).r;
		
		// 3. Per-Layer UV Calculation
		// 🟢 Parity Fix: Align shader with Standard UV (North = Top) to match Variation Tool.
		// Previous "-scaled_uv.y" caused a phase shift in Mirror Patterns.
		vec2 uv1 = vec2(scaled_uv.x * s1, scaled_uv.y * s1);
		uv1.x += row_offset; // Random row shift (0, 0.25, 0.5, 0.75)
		






		// 🟢 Ornate Floor Parity: TileGround03 (Index 4) Radial Diamond Pattern
		// (Based on user screenshot and "dashed lines around diamond" comment)
		// Radial Inward Logic (4-Way Symmetry):
		// (0,0) -> Rot0 | (1,0) -> Rot90
		// (0,1) -> Rot270 | (1,1) -> Rot180
		if (abs(idx1 - 4.0) < 0.1) {
			int gx = int(base_uv.x) % 2;
			int gy = int(base_uv.y) % 2;
			
			vec2 f = fract(uv1);
			vec2 uv_base = floor(uv1);
			
			if (gx == 0 && gy == 0) {
				// TL: Identity (Rot0)
				// No change
			} else if (gx == 1 && gy == 0) {
				// TR: Rot90 CW -> (v, 1-u)
				uv1 = uv_base + vec2(f.y, 1.0 - f.x);
			} else if (gx == 0 && gy == 1) {
				// BL: Rot270 CW -> (1-v, u)
				uv1 = uv_base + vec2(1.0 - f.y, f.x);
			} else {
				// BR: Rot180 -> (1-u, 1-v)
				uv1 = uv_base + vec2(1.0 - f.x, 1.0 - f.y);
			}
		}
		
		if (abs(idx1 - 5.0) < 0.1) { // TileWater
			if (v_slope > 0.5) { // Anti-Waterfall Projection
				if (abs(NORMAL.x) > abs(NORMAL.z)) uv1 = vec2(v_world_pos.z, v_world_pos.y) * 0.25;
				else uv1 = vec2(v_world_pos.x, v_world_pos.y) * 0.25;
			} else { // Flat Projection (Scrolling)
				uv1 = v_world_pos.xz * 0.25;
			}
			uv1.x += mu_world_time * 0.000025; // Sven Water Scroll (U-axis)
		}
		vec3 tex1 = texture(tile_textures, vec3(uv1, idx1)).rgb;
		
		// --- Layer 2 UV ---
		vec3 tex2;
		if (idx2 > 254.1) {
			tex2 = tex1;
		} else {
			vec2 uv2 = vec2(scaled_uv.x * s2, scaled_uv.y * s2);
			uv2.x += row_offset;
			
			if (abs(idx2 - 4.0) < 0.1) {
				int gx = int(base_uv.x) % 2;
				int gy = int(base_uv.y) % 2;
				
				vec2 f = fract(uv2);
				vec2 uv_base = floor(uv2);
				
				if (gx == 0 && gy == 0) {
					// Identity
				} else if (gx == 1 && gy == 0) {
					uv2 = uv_base + vec2(f.y, 1.0 - f.x); // Rot90 CW
				} else if (gx == 0 && gy == 1) {
					uv2 = uv_base + vec2(1.0 - f.y, f.x); // Rot270 CW
				} else {
					uv2 = uv_base + vec2(1.0 - f.x, 1.0 - f.y); // Rot180
				}
			}
			
			if (abs(idx2 - 5.0) < 0.1) {
				if (v_slope > 0.5) {
					if (abs(NORMAL.x) > abs(NORMAL.z)) uv2 = vec2(v_world_pos.z, v_world_pos.y) * 0.25;
					else uv2 = vec2(v_world_pos.x, v_world_pos.y) * 0.25;
				} else {
					uv2 = v_world_pos.xz * 0.25;
				}
				uv2.x += mu_world_time * 0.000025;
			}
			tex2 = texture(tile_textures, vec3(uv2, idx2)).rgb;
		}
	
		// 4. Alpha Blending
		float alpha = texture(alpha_map, map_uv_smooth).r;
		vec3 final_color = mix(tex1, tex2, alpha);
		
		// 5. Lighting & Shadows (Extreme Parity)
		vec3 light_v = texture(lightmap_map, map_uv_smooth).rgb * lightmap_intensity;
		vec3 shadow_v = texture(shadow_map, map_uv_smooth).rgb;
		
		// 🟢 Overlay Blend Parity:
		// MU detail maps add 'grit' and depth. Overlay preserves highlights better than Multiply.
		vec3 blend_src = clamp(shadow_v * 1.5, 0.0, 1.0);
		vec3 overlay;
		for (int i = 0; i < 3; i++) {
			if (light_v[i] < 0.5) overlay[i] = 2.0 * light_v[i] * blend_src[i];
			else overlay[i] = 1.0 - 2.0 * (1.0 - light_v[i]) * (1.0 - blend_src[i]);
		}
		
		light_v = mix(light_v, overlay, shadow_intensity);
		
		// Ensure a minimum ambient floor
		light_v = max(light_v, vec3(0.05));
		
		// 6. Water Specular
		float spec = 0.0;
		float w_mask = texture(water_map, map_uv_smooth).r;
		if (w_mask > 0.01) {
			vec2 reflect_uv = v_world_pos.xz * 0.5;
			reflect_uv.y += mu_world_time * 0.00005; // Independent ripple timing
			float ripples = texture(tile_textures, vec3(reflect_uv, 5.0)).r;
			spec = pow(ripples, 8.0) * w_mask * 0.15;
		}
		
		if (highlight_index >= 0 && (int(idx1 + 0.1) == highlight_index || (idx2 < 254.1 && int(idx2 + 0.1) == highlight_index))) {
			ALBEDO = vec3(1.0, 0.0, 1.0);
		} else if (debug_mode == 2) {
			ALBEDO = vec3(idx1 / 20.0, idx2 / 20.0, 0.0);
		} else if (debug_mode == 3) {
			ALBEDO = vec3(alpha);
		} else if (debug_mode == 5) {
			ALBEDO = light_v;
		} else if (debug_mode == 7) {
			ALBEDO = vec3(fract(uv1), 0.0);
		} else if (debug_mode == 6) {
			vec2 grid_f = fract(scaled_uv);
			vec3 grid_color = vec3(base_uv / 255.0, 0.0);
			if (grid_f.x < 0.05 || grid_f.y < 0.05) grid_color = vec3(1.0);
			ALBEDO = grid_color;
		} else {
			ALBEDO = (final_color * light_v) + spec;
		}
		
		EMISSION = vec3(0.0);
		SPECULAR = w_mask * 0.3;
		ROUGHNESS = 1.0 - w_mask * 0.8;
	}
	NORMAL = NORMAL; // Preserve vertex normals
}
