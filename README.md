# MU Online Remaster - C++ Edition

A native C++20 restoration of the MU Online engine for macOS with OpenGL 3.3+. Faithful migration from Main 5.2 source with server-side balance from OpenMU Version075.

## Features

- **Environment & Atmosphere**:
    - **Worlds**: Lorencia (Plains), Dungeon (Underground), and Devias (Snow).
    - **VFX Engine**: Hybrid **Particle Bursts** (Billboards) and **3D Skill Models** (BMD). Supports Alpha-blending, Ribbons, and additive glow passes.
    - **Ambient Life**: **Boid-driven** flocks of Birds (Lorencia), Bats (Dungeon), and Fish (Water) with procedural avoidance and "scare" behaviors.
    - **Weather**: Interactive grass (42k+ billboards), procedural fire, snow particles, and rift/void rendering.
- **Combat & Technical Depth**:
    - **Skills**: **20+ unique class skills** (DK/DW) with server-side damage math, AG/Mana costs, and AoE logic.
    - **Item Enhancement**: **Chrome Glow** system (+7 to +13) with multi-pass additive rendering (Chrome/Metal/Shiny).
    - **Animation Mastery**: **250+ unique animation sequences** in the main `Player.bmd`, including mount-specific riding poses.
    - **3D Audio**: OpenAL-powered **Positional Audio** with distance attenuation for monsters, ambient creatures, and environment.
- **Progression & Systems**:
    - **Skeletal Mounts**: Dynamic bone-attachment for **Uniria** and **Dinorant** with synced stride-logic and hoofbeat audio.
    - **Pet Companions**: Flying **Guardian Angels** (smooth orbit) and shoulder-mounted **Imps** with custom rendering passes.
    - **Quest System**: **18 linear quests** across all maps (Kill/Travel) with SQLite persistence.
    - **Asset Depth**: **900+ item definitions** across 15 categories, 1100+ WAV effects, and 70+ MP3 tracks.
- **Performance & Tools**:
    - **macOS Native**: Optimized for **OpenGL-on-Metal**, features native window activation and dynamic VBO sub-data updates.
    - **Media Capture**: High-performance JPEG screenshots and **Animated GIF** recording.
    - **Dev Tools**: Integrated `model_viewer`, `char_viewer`, and `diag` tools for asset and world validation.
- **Engineering Excellence**:
    - **Authoritative Server**: All combat, movement, and inventory logic is verified server-side via SQLite state.
    - **Pro AI**: Monsters feature a **7-state machine** with A* pathfinding and crowd-staggering logic.
    - **Living World**: Persistent **Quest Chains**, **Interactive Objects** (Sit/Pose), and **Chat History**.

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
| [item-and-vendor-system.md](docs/item-and-vendor-system.md) | 900+ Items, NPC Shops, Zen mechanics |
| [world-and-quest-system.md](docs/world-and-quest-system.md) | Map IDs, 18-quest progression, Boss encounters |
| [reference-navigation.md](docs/reference-navigation.md) | Key functions in original Main 5.2 |

## Technical Deep Dive

### VFX: Particle vs BMD Models
The VFX system (`VFXManager.cpp`) uses two distinct pipelines:
1. **Particle Bursts**: High-speed billboarded quads for blood, sparks, and smoke.
2. **3D BMD Skills**: Complex spells like *Fire Ball*, *Poison*, and *Ice* use standard BMD models with bone-calculated rotations and additive glow layers.

### Skeletal Dynamics & Mounts
Restored the complex bone-interdependency system from Main 5.2:
- **Vehicle Attachment**: Player bone 16 attaches to mount bone 1. Mounts include Uniria (Rider01) and Dinorant (Rider02), featuring independent animation blending and "Z-bounce" synchronization to keep the player seated during movement.
- **Pet Companions**: Flying pets use a direction-vector "Boid-like" follow algorithm with random wandering. Shoulder-mounted pets (Imp) use a dynamic spring-lerp to stick to the left shoulder bone.

### Ambient Life (Boid Engine)
The `BoidManager.cpp` simulates organic ambient movement for:
- **Birds & Bats**: Flocking behavior with separation, alignment, and cohesion. They react to the player's proximity, scattering when approached.
- **Fish**: Sub-surface swimming simulation in Lorencia's water tiles.

### Item Enhancement (ChromeGlow)
Restored Main 5.2's **part_object_color** logic:
- **+7 Enhancement**: Single pass `Chrome01.OZJ` (Additive).
- **+9 Enhancement**: Double pass `Chrome01` + `Shiny01` (Metal effect).
- **+11 to +13**: Triple pass adding `Chrome02` for high-intensity highlights.
Per-item primary/secondary color tables define unique glows for items like *Plate Armor* (Gold) or *Legendary* (Lavender).

### Audio Engine
OpenAL-soft implementation (`SoundManager.cpp`) with MP3 decoding via `minimp3`:
- **Positional 3D**: 1100+ WAV sounds are supported. `SOUND_BAT01`, `SOUND_BIRD02`, and monster agro sounds are world-space positioned with distance-based rolloff.
- **Seamless Crossfading**: 70+ high-quality MP3 music tracks crossfade during map transitions.

### Server-Side AI (7-State Machine)
The server (`GameWorld.cpp`) manages entity logic with high precision:
- **State Machine**: Monsters transition through **Idle**, **Wander**, **Chase**, **Approach**, **Attack**, **Return** (leash), and **Escape** (Dying).
- **Pathfinding**: High-performance **A* implementation** on 256x256 grids.
- **Crowd Control**: Attack stagger logic ensures multi-monster encounters don't overwhelm the network or player.

### Network Authority
The architecture follows a strict authoritative model:
- **Validation**: Server validates all client requests (Position, Equip, Use, Attack) against the SQLite database and world collision grid.
- **Sync**: Delta-compressed updates for viewports (monsters, NPCs, other players) ensure low-latency gameplay.

### macOS Optimization & Rendering
Tailored for modern macOS environments:
- **Metal Compatibility**: Uses `glBufferSubData` for dynamic mesh re-skinning, avoiding expensive buffer re-allocations that cause stutters on the OpenGL->Metal translation layer.
- **4-Tap Blending**: The terrain shader performs 4-tap bilinear blending for seamless tile transitions.
- **Bilinear Lightmap**: World objects and terrain share a 256x256 lightmap for baking smooth, high-fidelity shadows and illumination.

## Modding & Extensibility

### Database Schema (SQLite)
The server uses a clean, relational schema for persistence:
- **`characters`**: Stats, level, coordinates, and BLOB-encoded skill/potion bars.
- **`character_inventory` / `character_equipment`**: Normalized tables for item tracking with foreign key integrity.
- **`monster_spawns`**: Define server-side entity populations per map.

### Asset Pipeline
The engine supports legacy MU formats with high-performance loaders:
- **BMD (Models)**: Skeletal meshes with bone-based animation.
- **OZJ (Textures)**: Custom JPEG wrapper with a 24-byte header, decoded via `libjpeg-turbo`.
- **OZT (Textures)**: TGA-based textures with RLE support for alpha-mapped effects (wings, ghosts).

### Map Structure
Maps consist of five distinct synchronized files:
- **`TerrainHeight.OZB`**: 256x256 grayscale elevation data.
- **`EncTerrain.att`**: Encrypted collision grid using a custom XOR-shift cipher (MAP_XOR_KEY).
- **`EncTerrain.map`**: Tile mapping and alpha-blending layers.
- **`EncTerrain.obj`**: Static world object placements (trees, buildings).
- **`TerrainLight.OZJ`**: Baked 256x256 lightmap for environmental shading.

## Combat & Progression

### Battle Mathematics
Damage and hit resolution are calculated server-side (`StatCalculator.cpp`):
- **Physical Damage**: Based on **Strength** and **Dexterity** breakpoints (e.g., DK: `STR/4`, ELF: `(STR+DEX)/4`).
- **Magic Damage**: Scaled by **Energy** (`ENE/4`) and modulated by staff-specific **Magic Rise** percentages.
- **Hit Chance**: Authoritative hit resolution: `Chance = 1.0 - DefenseRate / AttackRate`. Minimum 3% floor applies.

### Character Meta-System
A classic 4-attribute system drives all progression:
- **Strength (STR)**: Primary physical damage and heavy armor requirements.
- **Dexterity (DEX)**: Drives **Attack Speed**, **Defense**, and **Success Rate**.
- **Vitality (VIT)**: Scaling factor for HP (e.g., DK: `3 HP/point`, DW: `2 HP/point`).
- **Energy (ENE)**: Drives **Mana**, **AG** (Stamina), and Wizardry power.

### Progression Curve
- **Leveling**: 400 levels with a non-linear experience curve stored in SQLite.
- **Point Allocation**: Players receive **5 points per level** (7 for MG) to manually distribute into attributes.
- **Hardcore Scaling**: Monsters 10+ levels above the player trigger an automatic **Dodging** state, requiring strategic progression.

## Visual Identity & Special Effects

### Chrome Glow & Item Scaling
The engine implements the iconic MU "glow" system for high-level equipment:
- **Multi-Pass Rendering**: +7 items trigger a single `CHROME` pass. +11 and above trigger up to **3 additive passes** using `Chrome01`, `Chrome02`, and `Shiny01` environment maps.
- **Color Mapping**: 60+ unique RGB definitions (`ChromeGlow.cpp`) ensure every armor set (Bronze to Dragon) has its distinct visual signature.
- **Dynamic Reflections**: Uses spherical environment mapping to fake high-fidelity metal reflections, optimized for the OpenGL-on-Metal translation layer.

### Transparency Engine
Specialized blending modes ensure high-performance transparency for complex meshes:
- **Vertex Alpha**: Wings, ghosts, and energy-based NPCs (like the Archangel) use per-vertex alpha modulation for smooth edges.
- **Additive Blending**: VFX like `Inferno` and `Evil Spirit` use `GL_ONE, GL_ONE` blending to create high-exposure, glowing magical effects.
- **Scripted Flags**: Texture filenames with `_R` (Bright), `_H` (Hidden), or `_N` (No-blend) suffixes are automatically parsed to set GPU pipeline state.

## Architecture & Security

### Packet Protocol (C1/C2/C3/C4)
The custom network layer implements a strict framing protocol for efficient data transmission:
- **C1/C3 (Standard)**: 3-byte header for small, low-latency packets (Login, Movement).
- **C2/C4 (Extended)**: 4-byte header supporting up to **65KB payloads** (Character list, Inventory sync, Viewport updates).
- **Legacy Compliance**: Implements the classic `BuxDecode` XOR-shift cipher for secure account/password exchange during the handshake.

### Network Orchestration
- **Non-Blocking I/O**: The server utilizes a central gated `poll()` loop to manage concurrent sessions without thread overhead.
- **Latency Optimization**: `TCP_NODELAY` is enforced on all client sockets to prevent Nagle's algorithm from buffering small combat packets.
- **Session Lifecycle**: Comprehensive `Session` state management with automatic buffer flushing and disconnect cleanup.

### Data Integrity & Persistence
- **Immediate Persistence**: Critical state changes (level up, item trade, map warp) trigger an immediate **SQLite atomic commit**, preventing gear loss on unexpected crashes.
- **Heartbeat & Autosave**: A background 60s tick ensures all active sessions are synchronized with the database.
- **Validation**: Server-side walkability checks (Grid-based collision) prevent speedhacking or "void walking" by matching client moves against `.att` terrain maps.

## Quests, Interaction & Social

### Quest System
A dedicated `QuestHandler` manages non-linear progression across multiple maps:
- **Persistent Chains**: Two primary chains (Lorencia and Devias) with 18+ unique stages including **Kill Tasks**, **Delivery**, and **Boss Takedowns**.
- **Server-Side Tracking**: Kill counts and quest states are persisted in the `character_quests` table, independent of client session.
- **Dynamic Rewards**: Automated distribution of Zen, experience, and class-specific item rewards (Skill Orbs/Scrolls).

### Social Integration & Chat
- **Persistent Chat Log**: All global and system messages are archived in SQLite, allowing the client to reload recent conversational context on login.
- **Color-Coded Feedback**: System-wide message system with custom hex-color support (e.g., Quest feedback in `0xFF64FFFF`).
- **Network Sync**: Low-latency chat broadcasting using the `Broadcast` orchestration layer.

### World Interaction (`OPERATE`)
The engine supports contextual world interactions beyond combat:
- **Interactive Objects**: Strategic placement of `SIT` and `POSE` triggers on world objects (Chairs, Boxes, Thrones).
- **Animation Sync**: Client-server handshake for state-based animations, ensuring players see each other sitting or interacting in real-time.
- **NPC Dialogs**: State-driven interaction with Guards and Vendors, featuring server-verified dialog ranges.

## Developer & Diagnostic Tools

The project includes specialized targets for development:

- `mu_client`: The main game client.
- `mu_server`: The authoritative game server.
- `model_viewer`: Standalone tool to inspect `.bmd` models and `.ozj` textures.
- `char_viewer`: High-fidelity character renderer for animation and armor set debugging.
- `diag_nearby`: Headless diagnostic tool for verifying server-side NPC/Monster viewport logic.
