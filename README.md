# MU Online Remaster - Godot 4 Engine

A high-fidelity migration of the MU Online engine to Godot 4, focused on high-performance rendering and authentic visual parity.

## Core Architecture

The engine has been restructured into a clean, modular API that loads original MU Online assets directly from the source.

- **100% Original Source Loading**: No pre-conversion step required. The engine parses `.bmd`, `.ozt`, `.ozj`, and `.att` files at runtime.
- **MUAPI Facade**: A centralized singleton for decoupled access to all subsystems (Data, Render, Mesh, World, Coords).
- **Forward+ Rendering**: Optimized for macOS (Metal) and modern GPUs, featuring cascaded shadows and atmospheric fog.

## Subsystems

- **[api/](file:///Users/karlisfeldmanis/Desktop/mu_remaster/addons/mu_tools/api/)**: Clean interfaces for engine features.
- **[parsers/](file:///Users/karlisfeldmanis/Desktop/mu_remaster/addons/mu_tools/parsers/)**: Native GDScript decrypter and parsers for MU legacy formats.
- **[rendering/](file:///Users/karlisfeldmanis/Desktop/mu_remaster/addons/mu_tools/rendering/)**: Specialized BMD mesh construction and texture management.
- **[world/](file:///Users/karlisfeldmanis/Desktop/mu_remaster/addons/mu_tools/world/)**: Heightmap rendering, global object spawning, and environment control.

## Visual Fidelity

- **Bilinear Snapping**: All objects and grass instances use interpolated terrain sampling for perfect ground alignment.
- **Authentic Atmosphere**: Pitch-black sky and fog parity matching the original "dark" MU aesthetics.
- **Global Spawning**: Procedural restoration of all 2,800+ original map objects.

## Quick Start

1. Place your original MU Online client files in `res://reference/MuMain/src/bin/Data/`.
2. Open the project in Godot 4.3+.
3. Launch `res://scenes/bootstrap.tscn`.

## Controls
- **WASD**: Move camera
- **Shift**: Speed up movement
- **Right Mouse**: Rotate camera
- **Mouse Wheel**: Zoom

## Documentation
- [Quick Start Guide](file:///Users/karlisfeldmanis/Desktop/mu_remaster/QUICKSTART.md)
- [Terrain Rendering Findings](file:///Users/karlisfeldmanis/Desktop/mu_remaster/SVEN_TERRAIN_FINDINGS.md)
- [Project Walkthrough (Refactor History)](file:///Users/karlisfeldmanis/.gemini/antigravity/brain/095a990b-287b-4760-b0ec-9d695da14e40/walkthrough.md)
