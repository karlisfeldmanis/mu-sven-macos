# MU Online Remaster - Quick Start

## Running the Game

**Launch the main application:**
```bash
godot scenes/main.tscn
```

Or simply open the project in Godot Editor and press F5.

## Project Structure

- **`scenes/main.tscn`** - Main game application (formerly render_test)
- **`scenes/forward_plus_test.tscn`** - Automated test for Forward+ rendering
- **`test_forward_plus.sh`** - Quick test script

## Features

- Forward+ renderer with Metal backend
- Lorencia world rendering with 2,870+ objects
- Directional lighting with cascaded shadows
- Atmospheric fog
- Free camera controls (WASD + mouse)

## Controls

- **WASD** - Move camera
- **Q/E** - Up/Down
- **Right Mouse** - Look around (hold)
- **Shift** - Move faster
- **V** - Toggle perspective/orthographic
- **P** - Screenshot
- **C** - Toggle follow/free cam
