#!/bin/bash

# MU Online Remaster - Preflight Shell Wrapper
# Runs the Godot preflight script and returns a non-zero exit code on failure.

PROJECT_DIR="/Users/karlisfeldmanis/Desktop/mu_remaster"

echo "=========================================="
echo "    MU Online Remaster Preflight Check    "
echo "=========================================="

# 1. Run Godot in headless mode with the preflight script
godot --headless --path "$PROJECT_DIR" -s scripts/preflight_check.gd

EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    echo "=========================================="
    echo "    PREFLIGHT PASSED: Ready to Launch     "
    echo "=========================================="
    exit 0
else
    echo "=========================================="
    echo "    PREFLIGHT FAILED: Fix errors above    "
    echo "=========================================="
    exit 1
fi
