@tool
extends Node

class_name MUTransformPipeline

## Authoritative math library for MU Online to Godot transformations.
## Solves coordinate drift and centralized swizzling logic.

# MU World Units per Tile/Meter = 100.0
# Godot default Meter = 1.0
const MU_SCALE = 0.01

# Standard Euler order (mapped from MU Z*Y*X to Godot YZX)
const MU_EULER_ORDER = EULER_ORDER_YZX

enum Space {
	MU_LOCAL,    # Raw BMD vertex/bone data (centimeters)
	MU_WORLD,    # Map placement data (centimeters, offset by 255 tiles)
	GODOT_LOCAL, # Centered at origin, Godot meters
	GODOT_WORLD  # Final placement in Godot scene
}

## Converts a local MU vector (raw BMD) to a local Godot vector (meters, swizzled).
## Swizzle: MU(X, Y, Z) -> Godot(-X, Z, -Y) [Mirror-X included]
static func local_mu_to_godot(v: Vector3) -> Vector3:
	return Vector3(v.x, v.z, -v.y) * MU_SCALE

## Converts a world MU vector (map data) to a world Godot vector (Y-up, Z-flipped).
## Parity with SVEN: Z = 255.0 - (Y / 100.0)
## Mirror Logic: X = 255.0 - (X / 100.0)
static func world_mu_to_godot(v: Vector3) -> Vector3:
	return Vector3(
		v.x * MU_SCALE,
		v.z * MU_SCALE,
		255.0 - (v.y * MU_SCALE)
	)

## Converts a local MU rotation (Euler radians) to a Godot Quaternion.
static func mu_rotation_to_quaternion(euler: Vector3) -> Quaternion:
	# Euler Swizzle matches local_mu_to_godot mapping (X, Z, -Y)
	var euler_godot = Vector3(euler.x, euler.z, -euler.y)
	return Basis.from_euler(euler_godot, MU_EULER_ORDER).get_rotation_quaternion()

## Builds a local Godot Transform3D from MU raw data.
static func build_local_transform(pos: Vector3, rot: Vector3) -> Transform3D:
	var q = mu_rotation_to_quaternion(rot)
	var p = local_mu_to_godot(pos)
	return Transform3D(Basis(q), p)

## Transforms a normal from local MU space to Godot space.
static func mu_normal_to_godot(n: Vector3, basis: Basis = Basis.IDENTITY) -> Vector3:
	# Swizzle raw normal [X-Mirror removed]
	var swizzled = Vector3(n.x, n.z, -n.y)
	# Apply transform basis (already in Godot space)
	return (basis * swizzled).normalized()
