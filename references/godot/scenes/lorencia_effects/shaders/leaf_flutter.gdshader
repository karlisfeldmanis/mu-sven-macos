shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled;

uniform sampler2D leaf_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform float flutter_speed : hint_range(0.0, 10.0) = 4.0;
uniform float flutter_amplitude : hint_range(0.0, 1.0) = 0.2;

varying float v_flutter;

void vertex() {
	// Add some random rotation based on time and instance ID (INSTANCE_ID not available in spatial vertex for GPUParticles3D unless specified, using custom data)
	// For simple fluttering, we can use VERTEX position + TIME
	float flutter = sin(TIME * flutter_speed + VERTEX.x * 2.0) * cos(TIME * flutter_speed * 1.2 + VERTEX.z * 1.5);
	VERTEX.y += flutter * flutter_amplitude;
	VERTEX.x += flutter * flutter_amplitude * 0.5;
	
	v_flutter = flutter;
}

void fragment() {
	vec4 tex = texture(leaf_texture, UV);
	
	// Alpha-clip for crisp leaf edges
	if (tex.a < 0.5) discard;
	
	// Subtle darkening in the shadows of the flutter
	ALBEDO = tex.rgb * (0.8 + 0.2 * v_flutter);
	ALPHA = tex.a;
	
	// Lorencia is a bit dark, let's keep it earthy
	ROUGHNESS = 0.8;
}
