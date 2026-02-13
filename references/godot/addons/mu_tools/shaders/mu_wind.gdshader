shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

global uniform float mu_wind_speed;
global uniform float mu_wind_scale;
global uniform vec3 mu_player_position;

uniform sampler2D texture_albedo : source_color, filter_nearest_mipmap;
uniform float global_alpha : hint_range(0.0, 1.0) = 1.0;

void vertex() {
	// Animation logic removed (Stage: Structural Alignment)
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
