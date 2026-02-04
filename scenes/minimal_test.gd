@tool
extends Node3D

@export_group("Minimal Test")
@export var test_checkbox: bool = false
@export_enum("One", "Two", "Three") var test_enum: String = "One"
@export_file("*.bmd") var test_file: String = ""

func _ready():
	print("Minimal test script ready")
