@tool
# class_name MUBMDRegistry
extends Node

## Global BMD Registry
##
## Caches parsed BMD files to prevent redundant I/O and processing.
## This significantly improves performance for frequently spawned effects
## or objects like the "Move Target" mark.

const BMDParser = preload("res://addons/mu_tools/parsers/bmd_parser.gd")

static var _bmd_cache: Dictionary = {} # Path (String) -> Object

## Returns a parsed BMDParser instance for the given path.
## If the BMD is not in the cache, it will be parsed and stored.
static func get_bmd(path: String, debug: bool = false) -> Object:
	if _bmd_cache.has(path):
		return _bmd_cache[path]
	
	var parser = BMDParser.new()
	if parser.parse_file(path, debug):
		_bmd_cache[path] = parser
		return parser
	
	return null

## Clears the BMD cache to free memory.
static func clear_cache() -> void:
	_bmd_cache.clear()
