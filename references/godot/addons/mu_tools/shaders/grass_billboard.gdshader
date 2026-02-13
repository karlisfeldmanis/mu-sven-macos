shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

global uniform float mu_world_time;
global uniform float mu_wind_speed;
global uniform float mu_wind_scale;
global uniform vec3 mu_player_position;

uniform sampler2D grass_texture : source_color;
uniform float global_alpha : hint_range(0.0, 1.0) = 0.8;

uniform sampler2D lightmap_texture : hint_default_black, filter_linear;

varying vec2 v_map_uv;
varying vec3 v_vertex_color;

void vertex() {
	v_vertex_color = COLOR.rgb; // Contains the AO gradient (root darkening)
	
	// 4. Windy Grass Patterns (SVEN High-Frequency Jitter)
	// MU X maps to Godot Z, MU Y maps to Godot X
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float mu_x = world_pos.z; // Godot Z = MU X
	float mu_y = world_pos.x; // Godot X = MU YV for lightmap sampling in fragment shader
	v_map_uv = vec2(mu_x / 256.0, mu_y / 256.0);
	
	// SVEN uses sinf(WindSpeed + xf * 50.f) for high-freq flutter
	float pattern = sin(mu_wind_speed + mu_x * 50.0);
	
	// Individual noise (COLOR.g) to break mechanical rows
	float sway_phase = pattern + (COLOR.g * 0.5);
	float sway = sin(sway_phase) * mu_wind_scale;
	
	// Height scale (0 at root, 1 at top)
	float height_scale = clamp(VERTEX.y / 0.4, 0.0, 1.0); 
	
	// SVEN Orthogonal Physics:
	// Shear is Z-only (MU X)
	VERTEX.z += -0.5 * height_scale;
	
	// Wind is X-only (MU Y)
	VERTEX.x += sway * 0.01 * height_scale;
	
	// Interaction: Push away from player
	float dist_to_player = distance(world_origin, mu_player_position);
	if (dist_to_player < 3.0) {
		vec3 push_dir = normalize(world_origin - mu_player_position);
		push_dir.y = 0.0;
		float push_strength = (1.0 - (dist_to_player / 3.0)) * height_scale;
		VERTEX.x += push_dir.x * push_strength * 1.5;
		VERTEX.z += push_dir.z * push_strength * 1.5;
	}
	
	// Atlas Slicing logic (4 x 1 slices)
	// SVEN Authentic Texture Cycling:
	// The UV window slides continuously based on Tile X (passed via COLOR.r)
	// COLOR.r contains (tile_x * 0.25) + row_random_offset
	float uv_width = 0.25;
	float atlas_offset = COLOR.r;
	
	// Remap UV.x from [0,1] to [0, 0.25] + offset
	UV.x = (UV.x * uv_width) + atlas_offset;
}

void fragment() {
	vec4 tex = texture(grass_texture, UV);
	
	// Sample lightmap for authentic bilayer lighting
	vec3 light_color = texture(lightmap_texture, v_map_uv).rgb;
	
	// Boost brightness: SVEN grass often pops brighter than terrain. 
	// Ensure we don't go below a certain baseline so it doesn't look black.
	light_color = max(light_color, vec3(0.5)); 
	light_color *= 1.5; // Slight over-brighten to match reference "pop"
	
	// Combined blending: Texture * Lightmap Tint * Vertex AO Gradient
	ALBEDO = tex.rgb * light_color * v_vertex_color;
	ALPHA = tex.a * global_alpha;
	
	// Softer clipping for cleaner edges with transparency
	ALPHA_SCISSOR_THRESHOLD = 0.1;
	
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
