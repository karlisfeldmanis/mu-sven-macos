extends Node
# MUCoordinateAPI.gd
# Centralized logic for coordinate system migration and mathematical utilities.

const MU_SCALE = 100.0 # Mathematically derived corner alignment scale

# Mirroring pivots (World 1 defaults)
var mirror_x_pivot: float = 256.0
var mirror_z_pivot: float = 256.0

## MU to Godot Space Conversion
func mu_to_godot(mu_vec: Vector3, mirror_x: bool = true, mirror_z: bool = false, offset: Vector3 = Vector3.ZERO) -> Vector3:
	var g_x = mu_vec.x / MU_SCALE
	var g_y = mu_vec.z / MU_SCALE # MU Z is height
	var g_z = mu_vec.y / MU_SCALE # MU Y is depth
	
	if mirror_x:
		g_x = mirror_x_pivot - g_x
	
	if mirror_z:
		g_z = mirror_z_pivot - g_z
		
	return Vector3(g_x, g_y, g_z) + offset

## Godot to MU Space Conversion
func godot_to_mu(godot_vec: Vector3, mirror_x: bool = true, mirror_z: bool = false, offset: Vector3 = Vector3.ZERO) -> Vector3:
	var local_vec = godot_vec - offset
	var g_x = local_vec.x
	var g_y = local_vec.y
	var g_z = local_vec.z
	
	if mirror_x:
		g_x = mirror_x_pivot - g_x
		
	if mirror_z:
		g_z = mirror_z_pivot - g_z
		
	return Vector3(g_x * MU_SCALE, g_z * MU_SCALE, g_y * MU_SCALE)

## Returns (0/1) parity for radial symmetry calculations
func get_grid_parity(x: int, y: int) -> Vector2:
	return Vector2(x % 2, y % 2)

## Utility to snap Godot position to MU Grid index
func pos_to_grid_index(pos: Vector3, mirror_x: bool = true) -> Vector2i:
	if mirror_x:
		return Vector2i(int(mirror_x_pivot - pos.x), int(pos.z))
	return Vector2i(int(pos.x), int(pos.z))
