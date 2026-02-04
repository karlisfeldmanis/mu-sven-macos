#!/usr/bin/env sh

# Forward+ Automated Test Runner
# Opens Godot with Forward+ renderer, loads a world, captures screenshot, and quits

WORLD_ID=${1:-World1}
DATA_PATH=${2:-res://reference/MuMain/src/bin/Data}

echo "==================================="
echo "Forward+ Automated World Renderer"
echo "==================================="
echo "World: $WORLD_ID"
echo "Data Path: $DATA_PATH"
echo ""

# Run Godot with window (required for Forward+/Vulkan)
# Quit after a few seconds via script
godot scenes/forward_plus_test.tscn -- "$WORLD_ID" "$DATA_PATH"

EXIT_CODE=$?
echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo "✓ Test completed successfully"
    echo "Check forward_plus_world*.png for output"else
    echo "✗ Test failed with code $EXIT_CODE"
fi

exit $EXIT_CODE
