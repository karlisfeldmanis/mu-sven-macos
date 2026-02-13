shader_type sky;

uniform sampler2D lightmap_tex;
uniform float world_time;

void sky() {
	vec3 dir = EYEDIR;
	float horizon = clamp(dir.y * 10.0, 0.0, 1.0);
	
	// Default MU Sky Blue
	vec3 sky_color = vec3(0.1, 0.3, 0.6);
	
	// Horizon reactive color (Sample terrain lightmap average)
	// For now, sample a fixed center point of the lightmap for reactive bloom
	vec3 light = texture(lightmap_tex, vec2(0.5, 0.5)).rgb;
	
	vec3 final_color = mix(light * 0.5, sky_color, horizon);
	
	COLOR = final_color;
}
