@tool
extends Node

class_name MUAssetAPI

## Centralized API for MU Online asset management.
## Orchestrates parsing, exporting, and runtime instantiation.

const BMDParser = preload("res://addons/mu_tools/core/bmd_parser.gd")

# OBJ instantiation removed — use native BMD loading via MUObjectManager

# Bulk validation (OBJ-based) removed.
