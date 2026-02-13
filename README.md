# MU Online Remaster - C++ Edition

A high-performance, native C++20 restoration of the MU Online engine, built with focus on rendering fidelity, smooth terrain blending, and direct legacy format compatibility. This project targets macOS as a primary platform with OpenGL 3.3+.

## Core Features

- **High-Fidelity Rendering**: Custom OpenGL 3.3+ renderer with native support for legacy MU effects, lighting, and alpha testing.
- **4-Tap Smooth Blending**: Advanced terrain shader that eliminates jagged tile boundaries through 4-tap bilinear index and attribute interpolation.
- **Advanced World Systems**: 
    - Full **BMD Object Rendering** with per-mesh blend states (Alpha, Bright/Additive, Hidden).
    - **Fire Effect System**: High-performance GPU-instanced billboarding for Lorenzian torches and bonfires.
    - **Interactive Object Browser**: Built-in ImGui browser to inspect every model in the `Data/Object1` directory.
- **Optimized Capture System**:
    - **GIF Animations**: High-performance recording with **consecutive frame diffing** and **dirty rectangle encoding**.
    - **Scale & Performance**: Integrated resolution downscaling (box filter) and frame skipping to reduce output size (e.g., 500KB → 20KB).
- **Universal BMD Support**: Native parsers for both version **0xC (encrypted)** and **0xA (unencrypted)** BMD models.

## Targets

### 1. MuRemaster
The main world viewer. Loads the full Lorencia (World1) map, including heightmaps, lightmaps, terrain textures, and over 2800 objects.

### 2. ModelViewer
A standalone object browser. Scans model directories and allows real-time inspection of BMD files, animations, and textures with orbit camera controls.

## Getting Started

### Prerequisites
- CMake 3.15+
- C++20 Compiler (Clang/GCC)
- giflib (for optimized GIF output)
- Original MU Online client data

### Quick Start
```bash
mkdir build && cd build
cmake ..
make -j
./MuRemaster  # Run world viewer
./ModelViewer # Run object browser
```

## Controls
- **WASD / Arrows**: Move camera / Navigate lists
- **Shift**: Speed up camera (Turbo)
- **Mouse Drag**: Rotate camera (Orbit in ModelViewer / FPS in World)
- **Mouse Wheel**: Zoom / Scroll
- **P**: Capture Screenshot (JPEG)
- **ImGui Panel**: Configure GIF Capture (ModelViewer)

## Project Structure
- `src/`: Native implementation files including BMD parsing, texture loading, and effect systems.
- `include/`: C++20 header definitions and data structures.
- `shaders/`: GLSL shaders for terrain, skeletal models, and billboard effects.
- `references/`: MU Online source of truth data and original implementation logic.
