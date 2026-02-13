# MU Online Remaster - C++ Edition

A high-performance, native C++ restoration of the MU Online engine, built with focus on rendering fidelity, smooth terrain blending, and direct legacy format compatibility.

## Core Features

- **High-Fidelity Rendering**: Custom OpenGL 3.3+ renderer designed for modern macOS (Metal-compatible via MoltenVK/GLFW).
- **4-Tap Smooth Blending**: Advanced terrain shader that eliminates jagged tile boundaries through 4-tap bilinear index and attribute interpolation.
- **Direct Asset Loading**: Native C++ parsers for `.bmd` (models), `.ozt/.ozj` (decrypted textures), `.map` (heightmaps), and `.att` (world mapping).
- **Authentic Visuals**: Restored lightmap integration and world-specific atmosphere settings.

## Getting Started

### Prerequisites
- CMake 3.15+
- C++17 Compiler
- OpenGL 3.3 compatible drivers
- Original MU Online client data (World1 for Lorencia)

### Quick Start
See [QUICKSTART.md](QUICKSTART.md) for build and run instructions.

## Controls
- **WASD**: Move camera
- **Shift**: Speed up camera (Turbo)
- **Right Mouse**: Rotate camera (Hold)
- **Mouse Wheel**: Zoom
- **F11**: Capture Screenshot

## Project Structure
- `src/`: Core implementation files (`main.cpp`, `Terrain.cpp`, etc.)
- `include/`: Header files and data structures
- `external/`: Dependencies (ImGui, GLM, etc.)
- `references/godot/`: Original Godot implementation (for logic reference)
- `references/other/MuMain/`: Source of truth client data
