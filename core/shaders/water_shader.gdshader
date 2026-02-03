shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back;

uniform sampler2D water_texture : source_color;
uniform vec4 water_tint : source_color = vec4(0.4, 0.6, 0.8, 0.7);
uniform float scroll_speed = 0.05;
uniform float wave_scale = 1.0;

void fragment() {
	// Animated UV scrolling
	vec2 uv = UV + vec2(TIME * scroll_speed, 0.0);
	
	// Sample water texture
	vec4 tex = texture(water_texture, uv * wave_scale);
	
	// Apply water tint
	ALBEDO = tex.rgb * water_tint.rgb;
	ALPHA = water_tint.a;
	
	// Water material properties
	METALLIC = 0.3;
	ROUGHNESS = 0.2;
	SPECULAR = 0.5;
}
