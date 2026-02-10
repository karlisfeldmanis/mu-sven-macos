shader_type spatial;
render_mode unshaded, cull_disabled;

uniform sampler2D height_map;
uniform float sx = 2000.0;
uniform float sy = 4000.0;
uniform vec3 body_origin;

void vertex() {
	vec4 world_pos = MODEL_MATRIX * vec4(VERTEX, 1.0);
	vec3 result = world_pos.xyz;
	
	// SVEN Planar Projection Formula
	// result[0] += result[2] * (result[0] + sx) / (result[2] - sy);
	// MU coords: Z is vertical. Godot coords: Y is vertical.
	// Mapping: MU X -> Godot X, MU Z -> Godot Y, MU Y -> Godot Z
	
	float rel_x = result.x - body_origin.x;
	float rel_y = result.y - body_origin.y;
	float rel_z = result.z - body_origin.z;
	
	// MU uses Z-up. In Godot Y-up:
	// rel_y is the MU-Z (vertical distance from character base)
	result.x += (rel_y * (result.x + sx)) / (rel_y - sy);
	
	// Snap to terrain height (Approximate using heightmap)
	// We need 0-1 UV for the heightmap sampling.
	vec2 terrain_uv = result.xz / 256.0; 
	float h = texture(height_map, terrain_uv).r * 255.0 * 1.5; // Scale matches mu_heightmap.gd
	
	result.y = h + 1.0; // Offset 1.0 to avoid z-fighting
	
	VERTEX = (inverse(MODEL_MATRIX) * vec4(result, 1.0)).xyz;
}

void fragment() {
	ALBEDO = vec3(0.0);
	ALPHA = 0.5;
}
