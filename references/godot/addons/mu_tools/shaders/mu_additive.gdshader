shader_type spatial;
render_mode blend_add, depth_draw_opaque, cull_disabled;

uniform sampler2D albedo_texture : source_color;
uniform bool use_alpha_scissor = false;
uniform float alpha_test_threshold : hint_range(0.0, 1.0) = 0.5;
uniform bool is_additive = false;
uniform bool black_is_transparent = false;
uniform float global_alpha = 1.0;

uniform bool use_wind = false;
uniform float wind_strength = 1.0;

// Global shader parameters
global uniform float mu_world_time;
global uniform float mu_wind_speed;
global uniform float mu_wind_scale;
global uniform vec3 mu_wind_direction;

varying vec4 v_color;

void vertex() {
	v_color = COLOR; // Capture MU vertex colors for Glow/Reinforcement
	
	if (use_wind) {
		// Only sway vertices that are above the origin (y > 0)
		float sway_mask = max(0.0, VERTEX.y);
		
		// Align with Sven grass sway logic
		float xf = NODE_POSITION_WORLD.x * 0.01;
		float sway = sin(mu_wind_speed + xf * 5.0) * mu_wind_scale;
		
		// Apply wind direction and strength
		// Note: Using 0.005 multiplier (5cm max) for generic world objects 
		// to feel localized and synchronized without being over-the-top.
		VERTEX.xz += mu_wind_direction.xz * sway * sway_mask * wind_strength * 0.005;
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
	
	if (use_alpha_scissor) {
		ALPHA = alpha_val * global_alpha;
		ALPHA_SCISSOR_THRESHOLD = alpha_test_threshold;
	} else {
		ALPHA = alpha_val * global_alpha;
	}
	
	// Handle Additive Blending (for Wings/Skills)
	if (is_additive) {
		// In Godot, true additive is handled via render_mode blend_add
		// but we can simulate it for characters if needed.
	}
}
