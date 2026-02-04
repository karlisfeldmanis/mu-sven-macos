# Shadow and Animation Diagnostics

## Current Configuration

### Shadow Settings (main.tscn)
- **DirectionalLight3D**: 
  - `shadow_enabled = true`
  - `directional_shadow_mode = 1` (Parallel split shadow maps)
  - `directional_shadow_max_distance = 500.0`
  - `shadow_bias = 0.05`
  - `shadow_normal_bias = 1.0`

- **DirectionalLight3D2** (fill light):
  - `shadow_enabled = false`

### Object Shadow Casting (mesh_builder.gd)
- Line 136: `mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON`
- All BMD objects should cast shadows

### Water Animation (water_shader.gdshader)
- `scroll_speed = 0.5` (increased 10x for visibility)
- `scroll_direction = vec2(0.3, 0.7)`
- Animation: `vec2 animated_uv = UV + scroll_direction * TIME * scroll_speed`
- Render mode: `lit` (not unshaded)

## Troubleshooting Steps

If shadows still don't appear:

1. **Check sun angle** - Shadows only visible when light hits objects at an angle
2. **Check camera position** - Must be close enough to see shadows (< 500 units from objects)
3. **Check terrain** - Terrain might be occluding object shadows
4. **Verify Forward+** - Confirm Metal/Vulkan renderer is active

If water doesn't animate:

1. **Check if water exists** - Look for water tiles in the world
2. **Water speed** - Now 10x faster, should be very obvious
3. **Texture** - Needs repeating pattern to show scrolling
4. **TIME uniform** - Should update automatically in Godot

## Testing in Editor

To test shadows manually:
1. Open `scenes/main.tscn` in Godot Editor
2. Run the scene (F5)
3.Select DirectionalLight3D node
4. Rotate it in the inspector to see shadow movement

To test water:
1. Look for blue water areas in the terrain
2. Water should have obvious flowing texture movement
3. Speed is 10x normal - very fast scrolling

## Next Steps if Still Not Working

1. Check Godot console for shader compilation errors  
2. Verify Forward+ renderer is actually active (check window title)
3. Try GL Compatibility mode to isolate Forward+ issues
4. Check project settings for shadow atlas size
