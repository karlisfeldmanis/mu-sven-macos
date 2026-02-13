shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled;

global uniform float mu_world_time;
global uniform float mu_water_move_uv;
global uniform float mu_wind_speed;
global uniform float mu_wind_scale;

uniform sampler2D water_texture : source_color, filter_linear, repeat_enable;
uniform float scroll_speed : hint_range(0.0, 2.0) = 0.1;
uniform float wave_amplitude : hint_range(0.0, 1.0) = 0.15;
uniform float wave_frequency : hint_range(0.0, 10.0) = 1.5;

varying float v_height;

void vertex() {
	// Strict Sven Water Vertical Displacement (sin + cos)
	float wave = sin(VERTEX.x * wave_frequency + mu_world_time * 0.002) * 
			cos(VERTEX.z * wave_frequency + mu_world_time * 0.0015);
	VERTEX.y += wave * wave_amplitude;
	v_height = wave;
}

void fragment() {
	// Strict Sven UV Migration Logic:
	// 1. Base UV with linear move (mu_water_move_uv)
	// 2. Ripple projection
	vec2 base_uv = UV + vec2(mu_water_move_uv, mu_water_move_uv);
	
	// Sven "Chrome Mapping" style projection
	vec2 chrome_uv = NORMAL.xy * 0.5 + 0.5;
	chrome_uv += base_uv * 0.1; // Blend projected with base uv
	
	vec4 tex = texture(water_texture, chrome_uv);
	
	ALBEDO = tex.rgb * COLOR.rgb;
	
	// Add highlight on wave peaks
	vec3 highlight = vec3(0.3, 0.4, 0.5) * max(0.0, v_height);
	ALBEDO += highlight;
	
	ALPHA = 0.75;
	METALLIC = 0.2;
	ROUGHNESS = 0.1;
}
