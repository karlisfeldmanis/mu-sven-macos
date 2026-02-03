@tool
class_name MUCoordinateUtils

## Coordinate System Conversion Utilities
##
## MuOnline uses Z-up coordinate system with 100x scale
## Godot uses Y-up coordinate system with standard scale

const SCALE_FACTOR = 0.01

## Converts a MuOnline position vector to Godot (scaling only)
## Internal skeletal system stays in Native MU space (Z-up)
static func convert_position(mu_pos: Vector3) -> Vector3:
	return mu_pos * SCALE_FACTOR

## Converts a MuOnline normal vector to Godot (scaling only)
static func convert_normal(mu_normal: Vector3) -> Vector3:
	return mu_normal.normalized()

## Ported from web-bmd-viewer bmd-loader.ts
## This ensures we match the reference implementation exactly for rotation logic
static func bmd_angle_to_quaternion(euler: Vector3) -> Quaternion:
	var half_x = euler.x * 0.5
	var half_y = euler.y * 0.5
	var half_z = euler.z * 0.5
	
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
