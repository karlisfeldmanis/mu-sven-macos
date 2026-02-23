# Quick Start Guide

## Prerequisites

- macOS with Xcode command line tools (`xcode-select --install`)
- CMake 3.15+ and Ninja build system (`brew install cmake ninja`)
- Dependencies: `brew install glfw glew libjpeg-turbo giflib glm sqlite3`
- Original MU Online client data — symlink or copy to `client/Data/`

## Building

### Server

```bash
cd server/build
cmake ..
ninja
```

### Client (always use Release)

```bash
cd client/build
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
```

Debug builds have significant rendering performance issues — always use Release.

## Running

Start the server first, then the client:

```bash
# Terminal 1: Server
cd server/build
./MuServer

# Terminal 2: Client
cd client/build
./MuRemaster
```

The server creates `mu_server.db` on first run with seeded data (items, NPCs, monsters). Delete this file to reset all progress.

## Data Directory

`client/Data/` is the canonical data directory. CMake auto-creates `build/Data` symlinks in both client and server build directories. Required assets:

- Kayito 0.97k base client (terrain, models, textures)
- Main 5.2 `Player.bmd` (247 actions) — must replace Kayito's version

## Default Character

New characters start as Dark Knight level 1 with:
- 1,000,000 Zen
- Short Sword equipped
- Small Shield equipped
- Default stats: STR 28, DEX 20, VIT 25, ENE 10

## Controls

| Key | Action |
|-----|--------|
| Left Click | Click-to-move, attack monster, talk to NPC, pick up items |
| Right Click | Skill attack with RMC slot skill |
| Q, W, E, R | Use potion from quick slot 1-4 |
| 1, 2, 3, 4 | Assign skill to RMC slot |
| C | Toggle character info / stat allocation |
| I | Toggle inventory |
| S | Toggle skill window |
| Esc | Close open panels |
| Mouse Wheel | Zoom camera in/out |

## Skill Learning

Buy skill orbs from the Potion Girl NPC shop, then right-click the orb in your inventory to learn the skill. Assign learned skills to the RMC slot using number keys 1-4.

## Clean Build

```bash
# Server
cd server/build && rm -rf * && cmake .. && ninja

# Client
cd client/build && rm -rf * && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja
```
