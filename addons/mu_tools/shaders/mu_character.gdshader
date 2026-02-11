shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled;

uniform sampler2D albedo_texture : source_color;
uniform bool use_alpha_scissor = false;
uniform float alpha_test_threshold : hint_range(0.0, 1.0) = 0.5;
uniform bool is_additive = false;
uniform bool black_is_transparent = false;

uniform bool use_wave = false;

// Global shader parameters
global uniform float mu_world_time;
global uniform float mu_wind_speed;
global uniform float mu_wind_scale;
global uniform vec3 mu_wind_direction;

varying vec4 v_color;

void vertex() {
	v_color = COLOR; // Capture MU vertex colors for Glow/Reinforcement
	
	if (use_wave) {
		// SVEN Parity (ZzzBMD.cpp:1354)
		// float time_sin = sinf((float)((int)WorldTime + source_vertex_index * 931) * 0.007f) * 28.0f;
		float time_sin = sin((mu_world_time + float(VERTEX_ID) * 931.0) * 0.007) * 28.0 * 0.01;
		VERTEX += NORMAL * time_sin;
	}
}

void fragment() {
	vec4 tex_color = texture(albedo_texture, UV);
	
	// Apply Vertex Color tinting (MU Glow effect/Lighting)
	// If vertex colors are black/unset (world objects), use white (full brightness)
	vec3 lighting = length(v_color.rgb) > 0.01 ? v_color.rgb : vec3(1.0);
	ALBEDO = tex_color.rgb * lighting;
	
	float alpha_val = tex_color.a;
	if (black_is_transparent && length(tex_color.rgb) < 0.1) {
		alpha_val = 0.0;
	}
	
	// Simulation of additive blending in blend_mix mode
	if (is_additive) {
		EMISSION = ALBEDO;
		alpha_val = length(tex_color.rgb);
	}
	
	if (use_alpha_scissor) {
		ALPHA = alpha_val;
		ALPHA_SCISSOR_THRESHOLD = alpha_test_threshold;
	} else {
		ALPHA = alpha_val;
	}
	
	// Handle Additive Blending (for Wings/Skills)
	if (is_additive) {
		// In Godot, true additive is handled via render_mode blend_add
		// but we can simulate it for characters if needed.
	}
}
