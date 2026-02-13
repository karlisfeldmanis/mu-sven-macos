extends Node
# MUCameraAPI.gd
# Interface for camera rig management and view control.

## Setup a standard 3D camera rig for the MU view
func setup_standard_camera(parent: Node3D) -> Node3D:
	var rig = Node3D.new()
	rig.name = "MUCameraRig"
	parent.add_child(rig)
	
	var cam = Camera3D.new()
	cam.name = "Camera3D"
	rig.add_child(cam)
	
	# Default MU-like orientation
	cam.position = Vector3(0, 70, 70)
	cam.look_at(Vector3.ZERO, Vector3.UP)
	
	return rig

## Focus the camera rig on a specific MU coordinate
func focus_on_mu_coordinate(rig: Node3D, mu_x: float, mu_y: float):
	const MUCoordinateUtils = preload("res://addons/mu_tools/core/coordinate_utils.gd")
	var target = MUCoordinateUtils.mu_to_godot(mu_x, mu_y)
	rig.global_position = target

## Set detailed view parameters
func set_view(rig: Node3D, yaw: float, pitch: float, zoom: float):
	rig.rotation_degrees.y = yaw
	var cam = rig.get_node("Camera3D")
	if cam:
		cam.rotation_degrees.x = pitch
		cam.position.z = zoom
		cam.position.y = zoom
