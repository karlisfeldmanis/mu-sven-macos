# MU Online Remaster - C++ Edition

A native C++20 restoration of the MU Online engine for macOS with OpenGL 3.3+. Faithful migration from Main 5.2 source with server-side balance from OpenMU Version075.

## Features

- **Multi-World Support**: 
    - **Lorencia**: 2800+ objects, procedural fire, interactive grass, and sky dome.
    - **Devias**: Rift/Void rendering, bridge safety fixes, and snow atmosphere.
- **Character Systems**:
    - **Classes**: Dark Knight (DK) and Dark Wizard (DW) with class-specific stats and visual assets.
    - **Progression**: Stat allocation, level-up effects, and skill-learning via orbs/scrolls.
    - **Mounts & Pets**: Support for Uniria, Dinorant, and Boid-based pet AI.
- **Combat & AI**:
    - **Combat**: Server-authoritative hits, DK/DW formulas, AG (Ability Gauge) system.
    - **Monster AI**: 7-state server machine, A* pathfinding, Pack Assist, and Chase Failure leashing.
- **UI & Interaction**:
    - **Character Select**: High-fidelity scene with FBO-rendered face portraits and interactive emotes.
    - **Diablo HUD**: Custom resource orbs (HP/AG/Mana) and expanded 13-slot quickbar (1-0, QWE).
    - **Message Log**: Persistent, filtered system/combat log with auto-fade and WoW-style interaction.
    - **Panels**: Full inventory/equipment (64 slots), skill window, NPC shops, and name markers.
- **Visuals & Media**:
    - **VFX**: Particle bursts, ribbon trails, and **Chrome Glow** (+7 item levels).
    - **Capture**: High-performance JPEG screenshots and animated GIF recording (frame-diffed).
- **Engine & Net**:
    - **Networking**: Binary TCP protocol with map-switching and OZT texture overlays.
    - **Persistence**: SQLite-backed server with 60s autosaves and item/monster databases.

## Project Structure

```
mu_remaster/
├── client/           # Game client
│   ├── src/          # Client source code
│   ├── include/      # Client headers
│   ├── shaders/      # GLSL shaders
│   ├── external/     # Third-party libs (imgui, stb)
│   ├── Data/         # Game assets (symlink to references/)
│   └── CMakeLists.txt
├── server/           # Game server
│   ├── src/          # Server source code
│   ├── include/      # Server headers
│   └── CMakeLists.txt
├── docs/             # Reference documentation
└── references/       # Original MU source (gitignored)
```

## Quick Start

### Prerequisites
- macOS with Xcode command line tools
- CMake 3.15+, Ninja (recommended)
- GLFW3, GLEW, libjpeg-turbo, giflib, GLM, SQLite3
- Original MU Online client data in `client/Data/`

### Build & Run

```bash
# One-click launch (handles build + data symlinks + process management)
./launch.sh
```

Alternatively, manual build:
```bash
# Server
cd server/build && cmake .. && ninja

# Client (always use Release)
cd client/build && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja
```

### Database

Server auto-creates `server/build/mu_server.db` on first run with seeded item definitions, NPC spawns, and monster spawns. Delete the DB file to reset.

## Controls

| Key | Action |
|-----|--------|
| Left Click | Move / Attack / Interact |
| Right Click | Skill attack (RMC slot) |
| Q, W, E, R | Use potion slots 1-4 |
| 1, 2, 3, 4 | Select skill to RMC slot |
| C | Toggle character info panel |
| I | Toggle inventory panel |
| S | Toggle skill window |
| Esc | Close panels / Cancel |
| Mouse Wheel | Camera zoom |

## Documentation

Detailed reference docs in `docs/`:

| Document | Content |
|----------|---------|
| [build-and-project.md](docs/build-and-project.md) | Build system, source files, architecture, packets |
| [character-system.md](docs/character-system.md) | DK stats, combat formulas, weapon config |
| [monster-system.md](docs/monster-system.md) | Monster types, AI, stats, movement |
| [bmd-format.md](docs/bmd-format.md) | BMD binary model format spec |
| [texture-formats.md](docs/texture-formats.md) | OZJ/OZT formats, texture resolution |
| [animation-system.md](docs/animation-system.md) | Tick-based timing, PlaySpeed tables |
| [rendering.md](docs/rendering.md) | Coordinate system, blend states, shadows |
| [terrain-and-environment.md](docs/terrain-and-environment.md) | Terrain, water, lightmap, grass, Devias rifts |
| [ui-system.md](docs/ui-system.md) | Diablo HUD, quickbar, map name displays, notifications |
| [reference-navigation.md](docs/reference-navigation.md) | Key functions in original Main 5.2 |
