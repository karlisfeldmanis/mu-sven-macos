shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

uniform sampler2D grass_texture : source_color;
uniform float wind_strength = 0.3;
uniform float wind_speed = 2.0;

void vertex() {
	// Apply wind animation to top vertices (Y > 0.5)
	if (VERTEX.y > 0.01) {
		float wind = sin(TIME * wind_speed + VERTEX.x * 0.5 + VERTEX.z * 0.5) * wind_strength;
		VERTEX.x += wind;
	}
}

void fragment() {
	vec4 tex = texture(grass_texture, UV);
	
	// Alpha cutout for grass transparency
	if (tex.a < 0.5) {
		discard;
	}
	
	// Apply vertex color (lightmap) and texture
	ALBEDO = tex.rgb * COLOR.rgb;
	ALPHA = tex.a;
	// Grass material properties
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
