shader_type spatial;

uniform sampler2DArray tile_textures : source_color;
uniform sampler2D layer1_map; // R8 texture containing tile indices
uniform sampler2D layer2_map; // R8 texture containing tile indices
uniform sampler2D alpha_map;  // R8 texture containing blend values

void fragment() {
	// Global UVs (0 to 1 over the whole terrain)
	vec2 global_uv = UV2;
	
	// Tile UVs (0 to 1 per tile)
	vec2 tile_uv = fract(UV);
	
	// Read indices from maps
	// Multiply by 255.0 to get the 0-255 index
	float idx1 = texture(layer1_map, global_uv).r * 255.0;
	float idx2 = texture(layer2_map, global_uv).r * 255.0;
	float alpha = texture(alpha_map, global_uv).r;
	
	// Sample both layers from the TextureArray
	vec3 tex1 = texture(tile_textures, vec3(tile_uv, idx1)).rgb;
	vec3 tex2 = texture(tile_textures, vec3(tile_uv, idx2)).rgb;
	
	// Blend based on alpha
	vec3 final_color = mix(tex1, tex2, alpha);
	
	// Apply vertex color (Pre-baked Lightmap)
	ALBEDO = final_color * COLOR.rgb * 2.0; // x2 for MU-style brightness?
	
	// MU usually has no metallic/roughness
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
