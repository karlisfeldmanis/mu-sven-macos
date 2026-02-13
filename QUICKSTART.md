# Quick Start Guide

## Building the Remaster

The project uses CMake for build management. 

1. **Configure and Build**:
   ```bash
   mkdir -p build
   cd build
   cmake ..
   make -j4
   ```

2. **Run**:
   ```bash
   ./MuRemaster
   ```

## Configuration

- **Data Path**: The application looks for original MU Data in `/Users/karlisfeldmanis/Desktop/mu_remaster/references/other/MuMain/src/bin/Data`.
- **World Loading**: Currently defaults to World 1 (Lorencia). To change worlds, modify `main.cpp`.
- **Camera State**: The camera position is saved to `camera_save.txt` on exit and restored on startup.

## Common Tasks

- **Take Screenshot**: Press `F11` or let the diagnostic timers trigger a capture. Screenshots are saved to `build/screenshots/`.
- **Debug View**: Toggle debug visualizations (Alpha, Lightmap, indices) via the ImGui overlay.
- **Clean Build**: 
  ```bash
  rm -rf build/
  mkdir build && cd build && cmake .. && make -j4
  ```
