extends Node
# MUAPI.gd
# Standardized global facade for the MU Remaster sub-systems.

var _data
var _render
var _mesh
var _coords
var _world
var _camera

func _init():
	pass

## World & Environment Sub-system
func world():
	if not _world:
		_world = load("res://addons/mu_tools/api/world_api.gd").new()
	return _world

## Data & Loading Sub-system
func data():
	if not _data:
		_data = load("res://addons/mu_tools/api/data_api.gd").new()
	return _data

## Rendering & Visuals Sub-system
func render():
	if not _render:
		_render = load("res://addons/mu_tools/api/render_api.gd").new()
	return _render

## Mesh & Model Construction Sub-system
func mesh():
	if not _mesh:
		_mesh = load("res://addons/mu_tools/api/mesh_api.gd").new()
	return _mesh

## Coordinate Math Sub-system (static utilities via MUCoordinateUtils)
func coords():
	if not _coords:
		_coords = load("res://addons/mu_tools/core/coordinate_utils.gd")
	return _coords

## Camera Management Sub-system
func camera():
	if not _camera:
		_camera = load("res://addons/mu_tools/api/camera_api.gd").new()
	return _camera
