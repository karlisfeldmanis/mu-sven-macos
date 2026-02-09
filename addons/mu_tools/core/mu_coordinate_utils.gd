@tool
class_name MUCoordinateUtils

## Coordinate System Conversion Utilities
##
## MuOnline: Z-up, 100 world units = 1 tile/meter. Vertex Y and Z are often swapped or require flip.
## Godot: Y-up, standard scale.
##
## SVEN Parity: Godot Z = 255 - Mu Y

const MUTransformPipeline = preload("res://addons/mu_tools/core/mu_transform_pipeline.gd")

const SCALE_FACTOR = 0.01
const MU_SCALE = 100.0
const HEIGHT_SCALE = 1.5

## Enum for standard MU Euler order (mapped to Godot)
const MU_EULER_ORDER = MUTransformPipeline.MU_EULER_ORDER

static func mu_to_godot(mu_x: float, mu_y: float, mu_height_byte: float = 0.0) -> Vector3:
	return Vector3(
		mu_x,
		(mu_height_byte * HEIGHT_SCALE) / MU_SCALE, # byte * 1.5 / 100.0
		255.0 - mu_y
	)

## Variant where height is already scaled to Godot meters
static func mu_to_godot_scaled(mu_x: float, mu_y: float, godot_y: float) -> Vector3:
	return Vector3(
		mu_x,
		godot_y,
		255.0 - mu_y # SVEN Parity: Godot Z = 255 - Mu Y
	)

## Converts MU world-space units (100 = 1 meter) to Godot space.
## Used for map object placement.
static func mu_world_to_godot(mu_vec: Vector3) -> Vector3:
	return MUTransformPipeline.world_mu_to_godot(mu_vec)

## Converts MU local vertex units to Godot space (no world offset)
static func mu_model_to_godot(mu_vec: Vector3) -> Vector3:
	return MUTransformPipeline.local_mu_to_godot(mu_vec)

## Converts MU tile coordinates to world center (meters)
static func tile_to_world(tile_x: int, tile_y: int, mu_height_byte: float = 0.0) -> Vector3:
	return mu_to_godot(float(tile_x) + 0.5, float(tile_y) + 0.5, mu_height_byte)

## Legacy aliases for compatibility
static func convert_position(mu_pos: Vector3) -> Vector3:
	return mu_world_to_godot(mu_pos)

static func convert_object_position(mu_pos: Vector3) -> Vector3:
	return mu_world_to_godot(mu_pos)

static func convert_scale(mu_scale: float) -> Vector3:
	# Reflect objects on X? Some MU clients reflect world objects.
	# For now, uniform scale is safest unless parity check fails.
	return Vector3(mu_scale, mu_scale, mu_scale)

static func convert_normal(mu_normal: Vector3) -> Vector3:
	return MUTransformPipeline.mu_normal_to_godot(mu_normal)

static func convert_basis(mu_basis: Basis) -> Basis:
	# Similarity Transform for MU orientation logic
	var x = mu_basis.x
	var y = mu_basis.y
	var z = mu_basis.z
	# Mapping (x,y,z) to Godot Y-up (X, Z, Y) 
	var g_x = Vector3(x.x, x.z, x.y)
	var g_y = Vector3(z.x, z.z, z.y)
	var g_z = Vector3(y.x, y.z, y.y)
	# Apply SVEN orientation flip for Z alignment?
	# Standard MU rotation results in objects facing -Z or +Z.
	return Basis(g_x, g_y, g_z)

static func convert_object_rotation(mu_euler: Vector3) -> Quaternion:
	# euler is in raw MU radians (swizzled in mu_rotation_to_quaternion)
	return bmd_angle_to_quaternion(mu_euler)

## Converts MU bone rotation (Euler radians) to Godot Quaternion.
##
## Correct Mapping (Mu -> Godot):
##   Mu X (Side) -> Godot X
##   Mu Z (Up)   -> Godot Y
##   Mu Y (Back) -> Godot -Z
## Order: YZX (mapped from MU's Z * Y * X)
static func bmd_angle_to_quaternion(euler: Vector3) -> Quaternion:
	return MUTransformPipeline.mu_rotation_to_quaternion(euler)

## Returns a local Godot Transform3D for a MU bone/object
static func get_mu_transform(pos: Vector3, rot: Vector3) -> Transform3D:
	var q = bmd_angle_to_quaternion(rot)
	# Position mapping matches mu_model_to_godot (convert MU units to meters)
	var p = Vector3(pos.x, pos.z, -pos.y) * SCALE_FACTOR
	return Transform3D(Basis(q), p)

const TERRAIN_SIZE = 256

static func tile_to_index(tile_x: int, tile_y: int) -> int:
	return tile_y * TERRAIN_SIZE + tile_x

static func index_to_tile(index: int) -> Vector2i:
	return Vector2i(index % TERRAIN_SIZE, index / TERRAIN_SIZE)

static func is_valid_tile(tile_x: int, tile_y: int) -> bool:
	return tile_x >= 0 and tile_x < TERRAIN_SIZE and tile_y >= 0 and tile_y < TERRAIN_SIZE

## Standard MU root rotation (X -90) to align Z-up data to Y-up Godot
static func get_root_rotation() -> Vector3:
	return Vector3(deg_to_rad(-90), 0, 0)

## Legacy aliases for compatibility with test scripts
static func mu_angle_to_godot_rotation(mu_angle: Vector3) -> Vector3:
	return bmd_angle_to_quaternion(mu_angle).get_euler(EULER_ORDER_XZY)

static func world_to_tile(world_pos: Vector3) -> Vector2i:
	return Vector2i(int(floor(world_pos.x)), int(floor(world_pos.z)))

static func mu_to_godot_position(mu_pos: Vector3) -> Vector3:
	return mu_to_godot(mu_pos.x / 100.0, mu_pos.y / 100.0, mu_pos.z)

static func godot_to_mu_position(godot_pos: Vector3) -> Vector3:
	return Vector3(godot_pos.x * 100.0, (255.0 - godot_pos.z) * 100.0, (godot_pos.y * 100.0) / 1.5)
