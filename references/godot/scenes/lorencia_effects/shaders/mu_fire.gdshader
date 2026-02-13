shader_type spatial;
render_mode blend_add, depth_draw_never, cull_disabled, unshaded;

uniform sampler2D fire_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 fire_color : source_color = vec4(1.2, 0.6, 0.2, 1.0);
uniform float scroll_speed : hint_range(0.0, 5.0) = 1.5;
uniform bool is_smoke = false;

void vertex() {
	// Standard Billboard (faces camera, stays upright)
	mat4 mat_world = MODEL_MATRIX;
	vec3 world_pos = mat_world[3].xyz;
	
	vec3 view_up = vec3(0.0, 1.0, 0.0);
	vec3 view_dir = normalize(CAMERA_POSITION_WORLD - world_pos);
	vec3 view_right = normalize(cross(view_up, view_dir));
	
	// Reconstruct model matrix for billboarding
	mat4 billboard_matrix = mat4(
		vec4(view_right * length(mat_world[0].xyz), 0.0),
		vec4(view_up * length(mat_world[1].xyz), 0.0),
		vec4(view_dir * length(mat_world[2].xyz), 0.0),
		vec4(world_pos, 1.0)
	);
	
	MODELVIEW_MATRIX = VIEW_MATRIX * billboard_matrix;
}

void fragment() {
	vec2 uv = UV;
	
	if (!is_smoke) {
		// MU fire textures often scroll upwards
		uv.y -= TIME * scroll_speed;
		// Add some horizontal jitter/wobble
		uv.x += sin(TIME * 5.0 + uv.y * 10.0) * 0.02;
	}
	
	vec4 tex = texture(fire_texture, uv);
	
	if (is_smoke) {
		// Smoke is usually more grey/dark and use standard alpha
		vec3 smoke_color = vec3(0.4, 0.4, 0.4);
		ALBEDO = tex.rgb * smoke_color * COLOR.rgb;
		ALPHA = tex.a * COLOR.a * 0.6;
	} else {
		// Particle color (alpha from particle process) feeds brightness
		vec3 final_color = tex.rgb * fire_color.rgb * COLOR.r * 2.5;
		ALBEDO = final_color;
		ALPHA = tex.a * COLOR.a;
	}
}
