shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

global uniform float mu_wind_speed;
global uniform float mu_wind_scale;
global uniform vec3 mu_player_position;

uniform sampler2D texture_albedo : source_color, filter_nearest_mipmap;
uniform float global_alpha : hint_range(0.0, 1.0) = 1.0;

void vertex() {
	// 4. Windy Grass Patterns (SVEN High-Frequency Jitter)
	// MU X maps to Godot Z, MU Y maps to Godot X
	vec3 world_origin = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float mu_x = world_origin.z + 255.0;
	float mu_y = world_origin.x;
	
	// SVEN uses sinf(WindSpeed + xf * 5.f)
	float pattern = sin(mu_wind_speed + mu_x * 5.0 + mu_y * 1.0);
	
	// Individual phase - using world position as noise source for objects
	float sway_phase = pattern + fract(world_origin.x * 13.0 + world_origin.z * 7.0);
	float sway = sin(sway_phase) * mu_wind_scale;
	
	// Strength depends on vertex relative height
	float height_scale = clamp(VERTEX.y * 0.5, 0.0, 1.0);
	
	// Apply sway
	VERTEX.x += sway * 0.005 * height_scale;
	VERTEX.z += sway * 0.005 * height_scale;
	
	// Interaction: Push away from player (Hellas-style)
	float dist_to_player = distance(world_origin, mu_player_position);
	if (dist_to_player < 3.0) {
		vec3 push_dir = normalize(world_origin - mu_player_position);
		push_dir.y = 0.0;
		float push_strength = (1.0 - (dist_to_player / 3.0)) * height_scale;
		VERTEX.x += push_dir.x * push_strength * 1.5;
		VERTEX.z += push_dir.z * push_strength * 1.5;
	}
}

void fragment() {
	vec4 albedo_tex = texture(texture_albedo, UV);
	ALBEDO = albedo_tex.rgb;
	// Support both per-vertex alpha and global manager alpha
	ALPHA = albedo_tex.a * global_alpha;
	
	// Lower scissoring to allow for softer look if requested, 
	// but keep it high enough to prevent Z-sorting artifacts for grass quads
	ALPHA_SCISSOR_THRESHOLD = 0.1;
}
