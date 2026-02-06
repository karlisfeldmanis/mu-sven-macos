@tool
class_name MUCoordinateUtils

## Coordinate System Conversion Utilities
##
## MuOnline uses Z-up coordinate system with 100x scale
## Godot uses Y-up coordinate system with standard scale

const SCALE_FACTOR = 0.01

static func convert_position(mu_pos: Vector3) -> Vector3:
	# Linear 1:1 Mapping: MU-X (Col) -> Godot X, MU-Y (Row) -> Godot Z, MU-Z (Up) -> Godot Y
	return Vector3(mu_pos.x, mu_pos.z, mu_pos.y) * SCALE_FACTOR

const HEIGHT_FACTOR = 1.0

## Converts MU object position to 1:1 world space
static func convert_object_position(mu_pos: Vector3) -> Vector3:
	# Linear 1:1 Mapping with 0.5m (50 MU units) centering offset.
	return Vector3(mu_pos.x * SCALE_FACTOR + 0.5, mu_pos.z * SCALE_FACTOR, mu_pos.y * SCALE_FACTOR + 0.5)

static func convert_basis(mu_basis: Basis) -> Basis:
	# Similarity Transform: R_g = M * R_m * M^-1
	# M = [-1, 0, 0; 0, 0, 1; 0, 1, 0] (Permutation + Reflection/Rotation)
	# This correctly maps MU rotation into Godot space while preserving Det=1.
	
	var x = mu_basis.x
	var y = mu_basis.y
	var z = mu_basis.z
	
	var g_x = Vector3(x.x, -x.z, -x.y)
	var g_y = Vector3(-z.x, z.z, z.y)
	var g_z = Vector3(-y.x, y.z, y.y)
	
	return Basis(g_x, g_y, g_z)

## Converts MU object rotation to Transpose (Mode 4) world space
static func convert_object_rotation(mu_euler: Vector3) -> Quaternion:
	# BMD Euler (X=Pitch, Y=Roll, Z=Yaw) to Matrix.
	var q_mu = bmd_angle_to_quaternion(mu_euler)
	var b_mu = Basis(q_mu)
	
	# Permute and rotate to Godot space (90-deg X rotation)
	return convert_basis(b_mu).get_rotation_quaternion()

## Converts a MuOnline normal vector to Godot (scaling only)
static func convert_normal(mu_normal: Vector3) -> Vector3:
	# (X=Col, Y=Row, Z=Height) -> (X=Col, Height=Y, Row=Z)
	return Vector3(mu_normal.x, mu_normal.z, mu_normal.y).normalized()

## Ported from SVEN BMD::AngleMatrix
## This ensures we match the reference implementation exactly for rotation logic
static func bmd_angle_to_quaternion(euler: Vector3) -> Quaternion:
	var sy = sin(euler.z)
	var cy = cos(euler.z)
	var sp = sin(euler.y)
	var cp = cos(euler.y)
	var sr = sin(euler.x)
	var cr = cos(euler.x)
	
	var m = Basis()
	m.x = Vector3(cp * cy, cp * sy, -sp)
	m.y = Vector3(sr * sp * cy + cr * -sy, sr * sp * sy + cr * cy, sr * cp)
	m.z = Vector3(cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp)
	
	return m.get_rotation_quaternion().normalized()

## Get the root rotation to convert from Z-up to Y-up
static func get_root_rotation() -> Vector3:
	# Rotate -90 degrees around X axis to convert Z-up to Y-up
	return Vector3(-PI / 2.0, 0, 0)

## Terrain tile utilities
const TERRAIN_SIZE = 256

static func tile_to_world(tile_x: int, tile_y: int, height: float = 0.0) -> Vector3:
	# Linear 1:1 Mapping:
	# MU-Col (tile_x) -> Godot X
	# MU-Row (tile_y) -> Godot Z
	return Vector3(
		float(tile_x) + 0.5, # Col -> Godot X
		height,              # Height -> Godot Y
		float(tile_y) + 0.5  # Row -> Godot Z
	)

## Convert Godot world position to tile coordinates
static func world_to_tile(world_pos: Vector3) -> Vector2i:
	return Vector2i(
		int(floor(world_pos.x)), # Godot X -> Tile X (col)
		int(floor(world_pos.z))  # Godot Z -> Tile Y (row)
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
