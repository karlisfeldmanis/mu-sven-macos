@tool
class_name MUCoordinateUtils

## Coordinate System Conversion Utilities
##
## MuOnline uses Z-up coordinate system with 100x scale
## Godot uses Y-up coordinate system with standard scale

const SCALE_FACTOR = 0.01

static func convert_position(mu_pos: Vector3) -> Vector3:
	# Mode 4 Transpose (Cyclic Permutation):
	# MU-X (Col) -> Godot Z
	# MU-Y (Row) -> Godot X
	# MU-Z (Up)  -> Godot Y
	return Vector3(mu_pos.y, mu_pos.z, mu_pos.x) * SCALE_FACTOR

const HEIGHT_FACTOR = 1.5  # Same as terrain heightmap scaling

## Converts MU object position to Transpose (Mode 4) world space
static func convert_object_position(mu_pos: Vector3) -> Vector3:
	# Same cyclic permutation, but with world-space extent offset for Z (Col)
	return Vector3(
		mu_pos.y * SCALE_FACTOR,
		mu_pos.z * SCALE_FACTOR,
		mu_pos.x * SCALE_FACTOR - (TERRAIN_SIZE - 1.0)
	)

## Converts MU rotation matrix/basis to Godot Transpose (Mode 4) space
static func convert_basis(mu_basis: Basis) -> Basis:
	# P maps MU units to Godot units:
	# MU-X (Col) -> Godot Z
	# MU-Y (Row) -> Godot X
	# MU-Z (Up)  -> Godot Y
	#
	# Godot.x = MU.y, Godot.y = MU.z, Godot.z = MU.x
	var g_x = Vector3(mu_basis.y.y, mu_basis.y.z, mu_basis.y.x)
	var g_y = Vector3(mu_basis.z.y, mu_basis.z.z, mu_basis.z.x)
	var g_z = Vector3(mu_basis.x.y, mu_basis.x.z, mu_basis.x.x)
	
	return Basis(g_x, g_y, g_z)

## Converts MU object rotation to Transpose (Mode 4) world space
static func convert_object_rotation(mu_euler: Vector3) -> Quaternion:
	# 1. Convert MU Euler to Basis (MU rotation matrix M)
	var q_mu = bmd_angle_to_quaternion(mu_euler)
	var b_mu = Basis(q_mu)
	
	# 2. Permute and rotate to Godot space
	return convert_basis(b_mu).get_rotation_quaternion()

## Converts a MuOnline normal vector to Godot (scaling only)
static func convert_normal(mu_normal: Vector3) -> Vector3:
	# Follows same cyclic permutation as position
	return Vector3(mu_normal.y, mu_normal.z, mu_normal.x).normalized()

## Ported from web-bmd-viewer bmd-loader.ts
## This ensures we match the reference implementation exactly for rotation logic
static func bmd_angle_to_quaternion(euler: Vector3) -> Quaternion:
	# MU BMD angles are ALREADY in radians for rotations
	var rad_x = euler.x
	var rad_y = euler.y
	var rad_z = euler.z
	
	var half_x = rad_x * 0.5
	var half_y = rad_y * 0.5
	var half_z = rad_z * 0.5
	
	var sin_x = sin(half_x)
	var cos_x = cos(half_x)
	var sin_y = sin(half_y)
	var cos_y = cos(half_y)
	var sin_z = sin(half_z)
	var cos_z = cos(half_z)
	
	var w = cos_x * cos_y * cos_z + sin_x * sin_y * sin_z
	var x = sin_x * cos_y * cos_z - cos_x * sin_y * sin_z
	var y = cos_x * sin_y * cos_z + sin_x * cos_y * sin_z
	var z = cos_x * cos_y * sin_z - sin_x * sin_y * cos_z
	
	return Quaternion(x, y, z, w).normalized()

## Get the root rotation to convert from Z-up to Y-up
static func get_root_rotation() -> Vector3:
	# Rotate -90 degrees around X axis to convert Z-up to Y-up
	return Vector3(-PI / 2.0, 0, 0)

## Terrain tile utilities
const TERRAIN_SIZE = 256

static func tile_to_world(tile_x: int, tile_y: int, height: float = 0.0) -> Vector3:
	# Verified Mode 4 (Transpose) Landmark Alignment:
	# MU-Row (tile_y) -> Godot X, MU-Col (tile_x) -> Godot Z
	return Vector3(
		float(tile_y) + 0.5, # Row -> Godot X
		height,              # Height -> Godot Y
		float(tile_x) - (TERRAIN_SIZE - 1.0) + 0.5 # Col -> Godot Z
	)

## Convert Godot world position to tile coordinates
static func world_to_tile(world_pos: Vector3) -> Vector2i:
	return Vector2i(
		int(floor(world_pos.z + (TERRAIN_SIZE - 1.0))),  # Godot Z -> Tile X (col)
		int(floor(world_pos.x))  # Godot X -> Tile Y (row)
	)

## Get linear index from 2D tile coordinates
static func tile_to_index(tile_x: int, tile_y: int) -> int:
	return tile_y * TERRAIN_SIZE + tile_x

## Get 2D tile coordinates from linear index
static func index_to_tile(index: int) -> Vector2i:
	return Vector2i(
		index % TERRAIN_SIZE,
		index / TERRAIN_SIZE
	)

## Validate tile coordinates are within bounds
static func is_valid_tile(tile_x: int, tile_y: int) -> bool:
	return tile_x >= 0 and tile_x < TERRAIN_SIZE and tile_y >= 0 and tile_y < TERRAIN_SIZE
