shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

global uniform float mu_wind_speed;
global uniform float mu_wind_scale;

uniform sampler2D texture_albedo : source_color, filter_nearest_mipmap;
uniform float global_alpha : hint_range(0.0, 1.0) = 1.0;

void vertex() {
	// 4. Windy Grass Patterns (refining for visible rolling waves)
	// MU X maps to Godot Z, MU Y maps to Godot X
	vec3 world_origin = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float mu_x = world_origin.z + 255.0;
	float mu_y = world_origin.x;
	
	// Create a 2D rolling wave pattern by combining X and Y phases
	// Frequency adjusted to 0.1 for broad rolling "patterns" (~62.8 units per wave cycle)
	float pattern = sin(mu_wind_speed + mu_x * 0.1 + mu_y * 0.05);
	
	// Individual phase from COLOR.g is not easily available for individual objects 
	// unless we pass it. For now we use world position noise.
	float sway = sin(pattern) * mu_wind_scale;
	
	// Strength depends on vertex relative height
	float height_scale = clamp(VERTEX.y * 0.5, 0.0, 1.0);
	
	// Apply sway
	VERTEX.x += sway * 0.01 * height_scale;
	VERTEX.z += sway * 0.005 * height_scale;
}

void fragment() {
	vec4 albedo_tex = texture(texture_albedo, UV);
	ALBEDO = albedo_tex.rgb;
	// Support both per-vertex alpha and global manager alpha
	ALPHA = albedo_tex.a * global_alpha;
	
	// Lower scissoring to allow for softer look if requested, 
	// but keep it high enough to prevent Z-sorting artifacts for grass quads
	ALPHA_SCISSOR_THRESHOLD = 0.1;
}
