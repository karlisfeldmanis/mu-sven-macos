shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

uniform sampler2D grass_texture : source_color;
uniform float wind_strength = 0.3;
uniform float wind_speed = 2.0;

void vertex() {
	// Billboard effect - make grass face camera
	// Keep Y-axis vertical, rotate around Y to face camera
	vec3 camera_dir = normalize(CAMERA_POSITION_WORLD - MODEL_MATRIX[3].xyz);
	camera_dir.y = 0.0;
	camera_dir = normalize(camera_dir);
	
	vec3 right = normalize(cross(vec3(0, 1, 0), camera_dir));
	vec3 up = vec3(0, 1, 0);
	vec3 forward = camera_dir;
	
	// Apply billboard rotation
	mat4 billboard = mat4(
		vec4(right, 0.0),
		vec4(up, 0.0),
		vec4(forward, 0.0),
		vec4(0.0, 0.0, 0.0, 1.0)
	);
	
	MODELVIEW_MATRIX = VIEW_MATRIX * MODEL_MATRIX * billboard;
	
	// Wind animation - only affect top vertices
	if (VERTEX.y > 0.5) {
		float wind = sin(TIME * wind_speed + VERTEX.x * 0.5 + VERTEX.z * 0.3) * wind_strength;
		VERTEX.x += wind;
	}
}

void fragment() {
	vec4 tex = texture(grass_texture, UV);
	
	// Alpha cutout for grass
	if (tex.a < 0.5) {
		discard;
	}
	
	// Apply vertex color (lightmap)
	ALBEDO = tex.rgb * COLOR.rgb;
	ALPHA = tex.a;
	
	// Grass material properties
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
