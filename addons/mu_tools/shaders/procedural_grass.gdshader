// Procedural Terrain Grass Billboard Shader (SVEN ZzzLodTerrain.cpp parity)
// Renders billboard quads placed on terrain tiles with wind animation.
// For object-based MultiMesh grass (types 20-27), see grass_billboard.gdshader.

shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque, unshaded;

global uniform float mu_wind_speed;
global uniform float mu_wind_scale;
global uniform vec3 mu_player_position;

// SVEN: LoadBitmap(BITMAP_MAPGRASS, GL_NEAREST, GL_REPEAT)
uniform sampler2D grass_texture : source_color, filter_nearest, repeat_enable;
uniform sampler2D lightmap_texture : hint_default_white, filter_linear;

varying vec2 v_map_uv;

void vertex() {
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;

	// Derive MU tile coordinates for wind formula
	// Godot Z = MU X, Godot X = MU Y
	float mu_x = world_pos.z;
	float mu_y = world_pos.x;

	// Lightmap UV (256x256 terrain grid)
	v_map_uv = vec2(mu_x / 256.0, mu_y / 256.0);

	// Height factor: UV.y=0 is top vertex, UV.y=1 is bottom vertex
	float is_top = 1.0 - UV.y;

	// SVEN Standard Wind: sin(WindSpeed + xf * 5.0) * WindScale
	// WindScale = 10.0 MU units → Godot: * 0.01
	// Applied to MU Y axis = Godot X axis
	float wind = sin(mu_wind_speed + mu_x * 5.0) * mu_wind_scale * 0.01;
	VERTEX.x += wind * is_top;

	// Player proximity push (SVEN GMHellas.cpp parity)
	float dist = distance(world_pos, mu_player_position);
	if (dist < 3.0) {
		vec3 push = normalize(world_pos - mu_player_position);
		push.y = 0.0;
		float strength = (1.0 - dist / 3.0) * is_top;
		VERTEX.x += push.x * strength * 1.5;
		VERTEX.z += push.z * strength * 1.5;
	}
}

void fragment() {
	vec4 tex = texture(grass_texture, UV);

	// Sample terrain lightmap (SVEN: PrimaryTerrainLight per-vertex)
	vec3 light = texture(lightmap_texture, v_map_uv).rgb;
	light = max(light, vec3(0.5));
	light *= 1.5;

	ALBEDO = tex.rgb * light;
	// SVEN: EnableAlphaTest() = GL_BLEND + SRC_ALPHA/ONE_MINUS_SRC_ALPHA
	// Smooth alpha blending, no hard scissor cutoff
	ALPHA = tex.a;

	METALLIC = 0.0;
	ROUGHNESS = 1.0;
}
