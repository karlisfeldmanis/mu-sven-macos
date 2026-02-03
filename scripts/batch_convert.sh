#!/bin/bash

## Batch Conversion Script
##
## Converts all MuOnline assets in raw_data/ to Godot-compatible formats
##
## Usage: ./scripts/batch_convert.sh

set -e

GODOT_BIN="${GODOT_BIN:-godot}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RAW_DATA_DIR="$PROJECT_DIR/raw_data"
ASSETS_DIR="$PROJECT_DIR/assets"

echo "========================================="
echo "MuOnline Asset Batch Converter"
echo "========================================="
echo "Project: $PROJECT_DIR"
echo "Godot: $GODOT_BIN"
echo ""

# Check if Godot is available
if ! command -v "$GODOT_BIN" &> /dev/null; then
    echo "Error: Godot not found. Set GODOT_BIN environment variable."
    echo "Example: export GODOT_BIN=/path/to/godot"
    exit 1
fi

# Check if raw_data exists
if [ ! -d "$RAW_DATA_DIR" ]; then
    echo "Error: raw_data directory not found: $RAW_DATA_DIR"
    exit 1
fi

# Convert textures
echo "Step 1: Converting textures..."
echo "-----------------------------------"
if [ -d "$RAW_DATA_DIR/Player" ]; then
    "$GODOT_BIN" --headless --path "$PROJECT_DIR" --script scripts/convert_textures.gd -- \
        "$RAW_DATA_DIR/Player" "$ASSETS_DIR/players/textures"
else
    echo "⚠ No Player directory found, skipping textures"
fi

echo ""

# Convert BMD files
echo "Step 2: Converting BMD models..."
echo "-----------------------------------"
if [ -d "$RAW_DATA_DIR/Player" ]; then
    for bmd_file in "$RAW_DATA_DIR/Player"/*.bmd; do
        if [ -f "$bmd_file" ]; then
            echo "Converting: $(basename "$bmd_file")"
            "$GODOT_BIN" --headless --path "$PROJECT_DIR" --script scripts/convert_bmd.gd -- \
                "$bmd_file" "$ASSETS_DIR/players/meshes"
        fi
    done
else
    echo "⚠ No BMD files found"
fi

echo ""
echo "========================================="
echo "Conversion Complete!"
echo "========================================="
echo "Check $ASSETS_DIR for converted files"
