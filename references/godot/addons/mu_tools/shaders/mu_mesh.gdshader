shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_lambert, specular_schlick_ggx;

uniform sampler2D albedo_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 albedo_color : source_color = vec4(1.0);
uniform float alpha_scissor_threshold : hint_range(0.0, 1.0) = 0.5;

// MU Effects
uniform bool use_chrome = false;
uniform bool use_wave = false;
uniform bool use_bright = false;
uniform bool use_pulsing = false;
uniform bool use_sawtooth = false;
uniform bool use_jitter = false;

// Wave Stiffness (0.0 = full wave, 1.0 = fixed)
uniform float wave_stiffness_top = 0.0;
uniform float wave_stiffness_center = 0.0;

// Scroll parameters
uniform float scroll_speed_u = 0.0;
uniform float scroll_speed_v = 0.0;
uniform float pulsing_speed = 4.0;
uniform float time_offset = 0.0;

varying vec3 v_normal_view;
varying float v_vertex_id;

void vertex() {
	v_normal_view = (VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
	v_vertex_id = float(VERTEX_ID);
	
	if (use_wave) {
		// SVEN Parity: ZzzBMD.cpp:1354
		// sin((WorldTime + vertex_id * 931) * 0.007) * 28.0
		float world_time = (TIME + time_offset) * 1000.0;
		float wave = sin((world_time + v_vertex_id * 931.0) * 0.007) * 0.28;
		
		float atten_y = 1.0 - (wave_stiffness_top * (1.0 - UV.y));
		float atten_x = 1.0 - (wave_stiffness_center * (1.0 - abs(UV.x - 0.5) * 2.0));
		
		VERTEX += NORMAL * wave * atten_y * atten_x;
	}
}

void fragment() {
	vec2 base_uv = UV;
	float cur_time = TIME + time_offset;
	
	// UV Scrolling
	if (use_sawtooth) {
		// Fountain Sawtooth Parity: -((int)WorldTime % 1000 * 0.001f)
		base_uv.x -= fract(cur_time * scroll_speed_u);
		base_uv.y -= fract(cur_time * scroll_speed_v);
	} else {
		// Standard Linear Scroll
		base_uv.x += cur_time * scroll_speed_u;
		base_uv.y += cur_time * scroll_speed_v;
	}
	
	vec2 chrome_uv = base_uv;
	if (use_chrome) {
		// SVEN Chrome Parity: ZzzBMD.cpp:1513/1518
		float wave = mod(cur_time * 1000.0, 10000.0) * 0.0001;
		chrome_uv.x = v_normal_view.y * 0.5 + wave;
		chrome_uv.y = v_normal_view.z * 0.5 + wave * 2.0;
	}
	
	vec4 tex_color = texture(albedo_texture, use_chrome ? chrome_uv : base_uv);
	
	vec3 color = tex_color.rgb * albedo_color.rgb;
	float alpha = tex_color.a * albedo_color.a;
	
	if (use_pulsing) {
		// SVEN Light Pulsing Parity: sin(WorldTime * 0.004) * 0.3 + 0.7
		float pulse = sin(cur_time * pulsing_speed) * 0.3 + 0.7;
		if (use_jitter) {
			// SVEN Window Flickering Parity: rand() % 4 + 4
			// Locked to 30Hz SVEN primary tick rate
			float tick_time = floor(cur_time * 30.0);
			float jitter = fract(sin(dot(vec2(tick_time), vec2(12.9898, 78.233))) * 43758.5453);
			pulse = mix(0.4, 0.8, jitter);
		}
		color *= pulse;
	}
	
	ALBEDO = color;
	ALPHA = alpha;
	
	if (use_bright) {
		// RENDER_BRIGHT logic: Unshaded emission
		EMISSION = ALBEDO * 0.8; 
	}
	
	if (ALPHA < alpha_scissor_threshold) {
		discard;
	}
}