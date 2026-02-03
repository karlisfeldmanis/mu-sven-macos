shader_type spatial; // Using standard spatial for character
render_mode blend_mix, depth_draw_opaque, cull_disabled;

uniform sampler2D albedo_texture : source_color;
uniform bool use_alpha_scissor = false;
uniform float alpha_test_threshold : hint_range(0.0, 1.0) = 0.5;
uniform bool is_additive = false;

varying vec4 v_color;

void vertex() {
	v_color = COLOR; // Capture MU vertex colors for Glow/Reinforcement
}

void fragment() {
	vec4 tex_color = texture(albedo_texture, UV);
	
	// Apply Vertex Color tinting (MU Glow effect)
	ALBEDO = tex_color.rgb * v_color.rgb;
	
	if (use_alpha_scissor) {
		ALPHA = tex_color.a;
		ALPHA_SCISSOR_THRESHOLD = alpha_test_threshold;
	} else {
		ALPHA = tex_color.a;
	}
	
	// Handle Additive Blending (for Wings/Skills)
	if (is_additive) {
		// In Godot, true additive is handled via render_mode blend_add
		// but we can simulate it for characters if needed.
	}
}
