@tool
extends Node

# class_name MUTransformPipeline

## Authoritative math library for MU Online to Godot transformations.
## Solves coordinate drift and centralized swizzling logic.

# MU units are typically centimeters.
const MU_LOCAL_SCALE = 0.01
const MU_WORLD_SCALE = 0.01 

# Unified Euler order: MU R_Z*R_Y*R_X conjugated by W gives Godot R_Y*R_X*R_Z
const MU_EULER_ORDER = 2 # EULER_ORDER_YXZ

enum Space {
	MU_LOCAL,    # Raw BMD vertex/bone data (centimeters)
	MU_WORLD,    # MU units are typically centimeters.
	GODOT_LOCAL, # Centered at origin, Godot meters
	GODOT_WORLD  # Final placement in Godot scene
}

## Converts a local MU vector (raw BMD) to a local Godot vector (meters, swizzled).
## Unified swizzle match world parity: MU(X, Y, Z) -> Godot(Y, Z, X) [Det=1, Cyclic Permutation]
static func local_mu_to_godot(v: Vector3) -> Vector3:
	return Vector3(v.y, v.z, v.x) * MU_LOCAL_SCALE

## Converts a world MU vector (map data) to a world Godot vector.
## Mapping: v.x=Row (MU_X), v.y=Col (MU_Y), v.z=Height
## Scaling: Auto-detects if input is in centimeters (>500) or meters.
## Alignment: MU_Y (Col) -> G_X (East), Height (Z) -> G_Y (Up), MU_X (Row) -> G_Z (North)
static func world_mu_to_godot(v: Vector3) -> Vector3:
	var s = 1.0
	# Auto-detect scale: if horizontal values are large, it's centimeters.
	if abs(v.x) > 500.0 or abs(v.y) > 500.0:
		s = 0.01
	return Vector3(
		v.y * s, # Godot X = MU Y (122)
		v.z * s, # Godot Y = Height (2.5)
		v.x * s  # Godot Z = MU X (129)
	)

## Converts a local MU rotation (Euler radians) to a Godot Quaternion.
## Uses direct matrix reconstruction of SVEN's AngleMatrix (ZYX Euler)
## then applies similarity transform W·M·Wᵀ to remap axes.
##
## SVEN AngleMatrix: angles[0]=X(roll), [1]=Y(pitch), [2]=Z(yaw), degrees internally.
## Our input is already in radians. SVEN builds M_mu = R_Z * R_Y * R_X.
##
## World position transpose: G_X=MU_Y, G_Y=MU_Z, G_Z=MU_X
## Permutation matrix W:  row0=(0,1,0), row1=(0,0,1), row2=(1,0,0)
## M_godot = W · M_mu · Wᵀ
static func mu_rotation_to_quaternion(euler: Vector3) -> Quaternion:
	# SVEN AngleMatrix (ZzzMathLib.cpp:194)
	# angles[2] = Yaw (around Z), indices for sin/cos: sy, cy
	# angles[1] = Pitch (around Y), indices: sp, cp
	# angles[0] = Roll (around X), indices: sr, cr
	
	var sr = sin(euler.x)
	var cr = cos(euler.x)
	var sp = sin(euler.y)
	var cp = cos(euler.y)
	# MU axes: X=east, Y=north, Z=up → X×Y=+Z → RIGHT-HANDED (same as Godot).
	# The cyclic permutation W (det=+1) maps between two RH systems,
	# so no sign flip is needed on any angle.
	var sy = sin(euler.z)
	var cy = cos(euler.z)
	
	# M_mu = Rz(yaw) * Ry(pitch) * Rx(roll)
	# SVEN uses column-major indexing [row][col] in a 3x4 float array
	# col0 (X-axis in MU local):
	var m00 = cp * cy
	var m10 = cp * sy
	var m20 = -sp
	
	# col1 (Y-axis in MU local):
	var m01 = sr * sp * cy - cr * sy
	var m11 = sr * sp * sy + cr * cy
	var m21 = sr * cp
	
	# col2 (Z-axis in MU local):
	var m02 = cr * sp * cy + sr * sy
	var m12 = cr * sp * sy - sr * cy
	var m22 = cr * cp
	
	# Similarity Transform: M_godot = W · M_mu · Wᵀ
	# Mapping MU Axis to Godot Axis:
	# MU X(0) -> G Z(2)
	# MU Y(1) -> G X(0)
	# MU Z(2) -> G Y(1)
	#
	# Formula: G[i][j] = M_mu[inv(i)][inv(j)] where inv: G_X(0)->MU_Y(1), G_Y(1)->MU_Z(2), G_Z(2)->MU_X(0)
	
	# WARNING: Pure similarity, NO sign flips. See docs/COORDINATE_SYSTEM.md
	# Sign flips here break fence/object rotations.
	var g00 = m11  # [1][1]
	var g01 = m12  # [1][2]
	var g02 = m10  # [1][0]

	var g10 = m21  # [2][1]
	var g11 = m22  # [2][2]
	var g12 = m20  # [2][0]

	var g20 = m01  # [0][1]
	var g21 = m02  # [0][2]
	var g22 = m00  # [0][0]
	
	# Build Godot Basis (Basis constructor takes columns)
	var basis = Basis(
		Vector3(g00, g10, g20), # Col 0
		Vector3(g01, g11, g21), # Col 1
		Vector3(g02, g12, g22)  # Col 2
	)
	return basis.get_rotation_quaternion()

## Builds a local Godot Transform3D from MU raw data.
static func build_local_transform(pos: Vector3, rot: Vector3) -> Transform3D:
	var q = mu_rotation_to_quaternion(rot)
	var p = local_mu_to_godot(pos)
	return Transform3D(Basis(q), p)

## Transforms a normal from local MU space to Godot space.
static func mu_normal_to_godot(n: Vector3, basis: Basis = Basis.IDENTITY) -> Vector3:
	# Cyclic permutation matching local_mu_to_godot: MU(X,Y,Z) -> Godot(Y,Z,X)
	var swizzled = Vector3(n.y, n.z, n.x)
	# Apply transform basis (already in Godot space)
	return (basis * swizzled).normalized()
