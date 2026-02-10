#!/bin/bash

# Verification Script for Lorencia Assets
# Launches Model Viewer for key assets and takes screenshots

mkdir -p assets/verification

GODOT_BIN="/Applications/Godot.app/Contents/MacOS/Godot"

echo "Verifying Beer01..."
"$GODOT_BIN" scenes/model_viewer.tscn --model "res://extracted_data/object_models/Beer01.obj" --out "assets/verification/verify_Beer01.png" --no-center

echo "Verification Complete. Check assets/verification/"
