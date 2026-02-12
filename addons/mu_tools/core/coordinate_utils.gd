@tool
# class_name MUCoordinateUtils

## Coordinate System Conversion Utilities
##
## MuOnline: Z-up, 100 world units = 1 tile/meter. Vertex Y and Z are often swapped or require flip.
## Godot: Y-up, standard scale.
##
## SVEN Parity: cyclic permutation (Y, Z, X) [Det=+1]
## Godot X = MU Y, Godot Y = MU Z, Godot Z = MU X

const MUTransformPipeline = preload("res://addons/mu_tools/core/transform.gd")

const SCALE_FACTOR = 0.01
const MU_SCALE = 100.0
const HEIGHT_SCALE = 1.5

## Enum for standard MU Euler order (mapped to Godot)
const MU_EULER_ORDER = MUTransformPipeline.MU_EULER_ORDER

static func mu_to_godot(mu_x: float, mu_y: float, mu_height_byte: float = 0.0) -> Vector3:
	return Vector3(
		mu_y,  # Godot X = MU Y (Row)
		(mu_height_byte * HEIGHT_SCALE) / MU_SCALE,
		mu_x   # Godot Z = MU X (Col)
	)

## Variant where height is already scaled to Godot meters
static func mu_to_godot_scaled(mu_x: float, mu_y: float, godot_y: float) -> Vector3:
	return Vector3(
		mu_y,     # Godot X = MU Y
		godot_y,
		mu_x      # Godot Z = MU X
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
	# WARNING: Must stay UNIFORM. See docs/COORDINATE_SYSTEM.md
	# Winding is fixed in mesh_builder.gd ([0,2,1]).
	# Negative axis here breaks fence/object rotations.
	return Vector3(mu_scale, mu_scale, mu_scale)

static func convert_normal(mu_normal: Vector3) -> Vector3:
	return MUTransformPipeline.mu_normal_to_godot(mu_normal)

static func convert_basis(mu_basis: Basis) -> Basis:
	# Similarity Transform for MU orientation logic: M_godot = W · M_mu · Wᵀ
	# Row/Col mapping: 0→1, 1→2, 2→0
	var x = mu_basis.x
	var y = mu_basis.y
	var z = mu_basis.z
	
	# Godot Frame elements from MU basis
	var g00 = y.y; var g01 = y.z; var g02 = y.x
	var g10 = z.y; var g11 = z.z; var g12 = z.x
	var g20 = x.y; var g21 = x.z; var g22 = x.x
	
	return Basis(
		Vector3(g00, g10, g20),
		Vector3(g01, g11, g21),
		Vector3(g02, g12, g22)
	)

static func convert_object_rotation(mu_euler: Vector3) -> Quaternion:
	# euler is in raw MU radians (swizzled in mu_rotation_to_quaternion)
	return bmd_angle_to_quaternion(mu_euler)

## Converts MU bone rotation (Euler radians) to Godot Quaternion.
## Order: ZYX (mapped from SVEN's AngleMatrix)
static func bmd_angle_to_quaternion(euler: Vector3) -> Quaternion:
	return MUTransformPipeline.mu_rotation_to_quaternion(euler)

## Returns a local Godot Transform3D for a MU bone/object
static func get_mu_transform(pos: Vector3, rot: Vector3) -> Transform3D:
	var q = bmd_angle_to_quaternion(rot)
	# Position mapping matches mu_model_to_godot (convert MU units to meters)
	var p = Vector3(pos.y, pos.z, pos.x) * SCALE_FACTOR
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
	# Inverse of transpose: MU_X = G_Z, MU_Y = G_X
	return Vector2i(int(floor(world_pos.z)), int(floor(world_pos.x)))

static func mu_to_godot_position(mu_pos: Vector3) -> Vector3:
	return mu_to_godot(mu_pos.x / 100.0, mu_pos.y / 100.0, mu_pos.z)

static func godot_to_mu_position(godot_pos: Vector3) -> Vector3:
	# Inverse of (G_X = MU_Y, G_Y = MU_Z, G_Z = MU_X)
	return Vector3(godot_pos.z * 100.0, godot_pos.x * 100.0, godot_pos.y * 100.0)

## Bilinear height sampling for world positions (Authentic MU Parity)
static func sample_height_bilinear(world_pos: Vector3, heights: PackedFloat32Array) -> float:
	if heights.is_empty(): return 0.0
	
	# Mapping (G_X = MU_Y, G_Z = MU_X) -> Inverse: MU_X = G_Z, MU_Y = G_X
	var mu_x = world_pos.z
	var mu_y = world_pos.x
	
	# Bounds check
	if mu_x < 0 or mu_x >= TERRAIN_SIZE or mu_y < 0 or mu_y >= TERRAIN_SIZE:
		return 0.0
	
	var xi = int(mu_x)
	var yi = int(mu_y)
	var xd = mu_x - float(xi)
	var yd = mu_y - float(yi)
	
	xi = clampi(xi, 0, TERRAIN_SIZE - 2)
	yi = clampi(yi, 0, TERRAIN_SIZE - 2)
	
	var idx1 = yi * TERRAIN_SIZE + xi
	var idx2 = yi * TERRAIN_SIZE + (xi + 1)
	var idx3 = (yi + 1) * TERRAIN_SIZE + xi
	var idx4 = (yi + 1) * TERRAIN_SIZE + (xi + 1)
	
	if idx4 >= heights.size(): return 0.0
		
	var h1 = heights[idx1]
	var h2 = heights[idx2]
	var h3 = heights[idx3]
	var h4 = heights[idx4]
	
	var left = h1 + (h3 - h1) * yd
	var right = h2 + (h4 - h2) * yd
	return left + (right - left) * xd
