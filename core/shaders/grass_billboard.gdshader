shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque;

global uniform float mu_world_time;
global uniform float mu_wind_speed;
global uniform float mu_wind_scale;

uniform sampler2D grass_texture : source_color;

void vertex() {
	// Strict Sven Sway Logic (ZzzLodTerrain.cpp)
	// Sven Y (Godot X) += TerrainGrassWind
	// TerrainGrassWind = sinf(WindSpeed + xf * 50.f) * WindScale
	
	// 4. Sway logic (Wind)
	// Sven Logic (ZzzLodTerrain.cpp:2430): 
	// TerrainGrassWind[Index] = sinf(WindSpeed + xf * 5.f) * WindScale;
	// xf is tile coordinate (Sven X). MU X maps to Godot Z (-255 to 0)
	float xf = NODE_POSITION_WORLD.z + 255.0; 
	float sway = sin(mu_wind_speed + xf * 5.0) * mu_wind_scale;
	
	// Apply sway on Godot X-axis (Sven Y) 
	// Note: We multiply by 0.01 to convert from MU units to Godot scale 
	// (Sven WindScale 10.0 = 0.1 Godot units = 10% of a tile)
	VERTEX.x += sway * 0.01;
	
	// Atlas Slicing logic (4 x 1 slices)
	UV.x = (UV.x * 0.25) + COLOR.r;
}

void fragment() {
	vec4 tex = texture(grass_texture, UV);
	
	if (tex.a < 0.5) {
		discard;
	}
	
	ALBEDO = tex.rgb;
	ALPHA = tex.a;
	
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
