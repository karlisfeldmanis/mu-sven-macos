shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, unshaded;

uniform sampler2D water_texture : source_color, filter_linear, repeat_enable;
uniform float scroll_speed = 0.05;
uniform vec2 scroll_direction = vec2(0.3, 0.7);

void fragment() {
	// Animated UV scrolling for flowing water effect
	vec2 animated_uv = UV + scroll_direction * TIME * scroll_speed;
	
	vec4 tex = texture(water_texture, animated_uv);
	
	// Apply vertex color (lightmap) and texture
	ALBEDO = tex.rgb * COLOR.rgb;
	ALPHA = 0.8;  // Semi-transparent water
	
	// Water material properties
	METALLIC = 0.0;
	ROUGHNESS = 0.3;  // Slightly reflective
}
