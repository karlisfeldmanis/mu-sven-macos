# MU Online Remaster - Quick Start

## Running the Engine

1. **Bootstrap the World**:
   Open the project in Godot and launch the main entry point:
   ```bash
   godot res://scenes/bootstrap.tscn
   ```
   *This scene initializes the modular API and loads the world directly from original sources.*

2. **Standalone Mode**:
   For a streamlined experience without the editor UI, run:
   ```bash
   godot res://main.tscn
   ```

## Key Features
- **Zero Build Time**: Direct parsing of `.bmd`, `.ozt`, and `.ozj` files.
- **2,800+ Objects**: Global restoration of all world assets.
- **Visual Parity**: Pitch-black sky, volumetric fog, and bilinear terrain snapping.

## Controls
- **WASD**: Move camera
- **Shift**: Move faster (Turbo)
- **Right Mouse**: Rotate camera (Hold)
- **Mouse Wheel**: Zoom In/Out
- **ESC**: Quit Application

## Directory Guide
- `addons/mu_tools/`: The core engine logic and parsers.
- `reference/`: Place your original MU Data files here.
- `scenes/`: Contains entry points and visual effects.
- `main.gd`: The standalone application controller.
