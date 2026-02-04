shader_type spatial;
render_mode cull_disabled;

uniform sampler2DArray tile_textures : source_color;
uniform sampler2D layer1_map : filter_nearest; // R8 texture containing tile indices
uniform sampler2D layer2_map : filter_nearest; // R8 texture containing tile indices
uniform sampler2D alpha_map : filter_linear;  // R8 texture containing blend values
uniform sampler2D grass_offset_map : filter_nearest; // R8 random UV offsets
uniform float lightmap_intensity : hint_range(0.0, 5.0) = 1.0;

void fragment() {
	// Global UVs (0 to 1 over the whole terrain)
	vec2 global_uv = UV2;
	
	// Tile UVs (0 to 1 per tile)
	vec2 tile_uv = fract(UV);
	
	// Read indices from maps
	float idx1 = texture(layer1_map, global_uv).r * 255.0;
	float idx2 = texture(layer2_map, global_uv).r * 255.0;
	
	// Reverted to simple mapping for visual clarity
	float grass_offset = texture(grass_offset_map, global_uv).r;
	vec2 layer1_uv = vec2(fract(UV.x) + grass_offset, fract(UV.y));
	
	// Sample both layers from the TextureArray
	vec3 tex1 = texture(tile_textures, vec3(layer1_uv, idx1)).rgb;
	vec3 tex2 = texture(tile_textures, vec3(tile_uv, idx2)).rgb;
	
	// Blend based on alpha
	float alpha = texture(alpha_map, global_uv).r;
	vec3 final_color = mix(tex1, tex2, alpha);
	
	// ALBEDO-only lighting for shadow testing
	// We'll multiply the final_color by the lightmap here to see if shadows appear on the darker surface
	vec3 lightmap = clamp(COLOR.rgb * lightmap_intensity, vec3(0.1), vec3(1.0));
	
	ALBEDO = final_color * lightmap;
	EMISSION = vec3(0.0);
	
	SPECULAR = 0.0;
	ROUGHNESS = 1.0;
}
