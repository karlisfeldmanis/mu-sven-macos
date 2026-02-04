shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

global uniform float mu_world_time;
global uniform float mu_wind_speed;
global uniform float mu_wind_scale;

uniform sampler2D grass_texture : source_color;
uniform float global_alpha : hint_range(0.0, 1.0) = 0.8;

void vertex() {
	// 4. Windy Grass Patterns (refining for visible rolling waves)
	// MU X maps to Godot Z, MU Y maps to Godot X
	vec3 world_origin = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float mu_x = world_origin.z + 255.0;
	float mu_y = world_origin.x;
	
	// Create a 2D rolling wave pattern by combining X and Y phases
	// Frequency adjusted to 0.1 for broad rolling "patterns" (~6.3 tiles per wave cycle)
	float pattern = sin(mu_wind_speed + mu_x * 0.1 + mu_y * 0.05);
	
	// Add individual noise (COLOR.g) to break mechanical rows
	// but keep it subtle (0.3 weight) so the "pattern" remains dominant
	float sway_phase = pattern + (COLOR.g * 0.3);
	float sway = sin(sway_phase) * mu_wind_scale;
	
	// Apply sway on Godot X-axis (MU Y) with a bit of Z (MU X) for depth
	// Note: Vertex.y-based strength is built-in to the billboard quad's relative vertices
	// (Vertices at bottom are near 0 relative Y, top are further)
	float height_scale = clamp(VERTEX.y * 1.5, 0.0, 1.0); 
	VERTEX.x += sway * 0.01 * height_scale;
	VERTEX.z += sway * 0.005 * height_scale; // Extra depth sway
	
	// Atlas Slicing logic (4 x 1 slices)
	UV.x = (UV.x * 0.25) + COLOR.r;
}

void fragment() {
	vec4 tex = texture(grass_texture, UV);
	
	ALBEDO = tex.rgb;
	ALPHA = tex.a * global_alpha;
	
	// Softer clipping for cleaner edges with transparency
	ALPHA_SCISSOR_THRESHOLD = 0.1;
	
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
