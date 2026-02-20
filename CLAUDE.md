# MU Online Remaster - Reference Library

Native C++20 restoration of the MU Online engine for macOS with OpenGL 3.3+.
Reference source: Main 5.2. We are **migrating, not innovating** -- stick to the original source as the source of truth.
Server-side balance reference: **OpenMU Version075** (`references/OpenMU/`) -- use for monster stats, spawn data, item definitions, and combat formulas.

## Build

```bash
cd build && cmake .. && make -j$(sysctl -n hw.ncpu)
cd server_build && cmake ../server && make -j$(sysctl -n hw.ncpu)
```

## Architecture Overview

Client uses modular namespace/class design with non-owning pointer context structs for cross-module communication:

- **main.cpp** (~1700 lines) -- Orchestrator: init, render loop, shutdown
- **ItemDatabase** -- 293 static item definitions, all lookup functions
- **ItemModelManager** -- 3D item model cache, UI + world rendering
- **InventoryUI** -- Inventory/equipment panels, drag-drop, tooltips, stat allocation
- **InputHandler** -- GLFW callbacks, mouse/keyboard dispatch
- **RayPicker** -- Mouse ray-cast: terrain, NPC, monster, ground item picking
- **GroundItemRenderer** -- Ground drop physics/models/labels, floating damage numbers
- **ClientPacketHandler** -- Incoming packet dispatch (initial sync + game loop)
- **ServerConnection** -- Typed send API (SendAttack, SendPickup, SendEquip, etc.)

Server uses handler-based architecture with SQLite persistence:
- **PacketHandler** routes to **CharacterHandler**, **CombatHandler**, **InventoryHandler**, **WorldHandler**
- **Database** manages SQLite (characters, items, NPCs, monsters)
- **GameWorld** manages terrain attributes, safe zones, monster AI
- **StatCalculator** implements DK stat formulas

## Documentation Index

Detailed reference docs are in `docs/`:

| Document | Content |
|----------|---------|
| [docs/build-and-project.md](docs/build-and-project.md) | Build system, source file index, architecture, packet protocol, data paths |
| [docs/bmd-format.md](docs/bmd-format.md) | BMD binary model format spec, encryption, byte layout |
| [docs/texture-formats.md](docs/texture-formats.md) | OZJ/OZT formats, texture resolution priority, script flags |
| [docs/animation-system.md](docs/animation-system.md) | Tick-based timing, PlaySpeed tables (monsters + players), world object animation |
| [docs/monster-system.md](docs/monster-system.md) | Monster type mapping, stats, movement, hover, bounding boxes, AI architecture |
| [docs/character-system.md](docs/character-system.md) | DK stats, Player.bmd action indices, weapon config, combat formulas |
| [docs/rendering.md](docs/rendering.md) | Coordinate system, blend states, shadows, world objects, EncTerrain.obj format |
| [docs/terrain-and-environment.md](docs/terrain-and-environment.md) | Terrain, water, lightmap, point lights, BlendMesh, fire, grass, luminosity |
| [docs/reference-navigation.md](docs/reference-navigation.md) | Key functions and files in original Main 5.2 source |

## Critical Rules

1. **objectAlpha uniform**: Any renderer using `model.frag` MUST set `objectAlpha` to 1.0 or objects will be invisible.
2. **GLEW before GLFW**: `GL/glew.h` must be included before `GLFW/glfw3.h`.
3. **glBufferSubData for macOS**: Use `glBufferSubData` for dynamic VBO updates, not `glBufferData` + VAO re-setup.
4. **No invented systems**: Do not add rendering/gameplay systems that don't exist in the original source. For example, Lorencia water is just an animated tile -- no water overlay.
5. **Tick-based engine**: Original runs at 25fps (40ms per tick). Animation and movement are per-tick, not delta-time scaled. PlaySpeed and MoveSpeed are per-tick values.
6. **Monster AI is server-side**: Client receives TargetX/TargetY and pathfinds there. Client does NOT decide when to chase or wander.
7. **BodyHeight = 0 for ALL monsters**: No body offset. Hover (Budge Dragon only) is separate.
8. **Module pattern**: New subsystems use `namespace Foo { Init(ctx); ... }` or `class Foo { static Init(); ... }` with context structs for shared state. No global "GameContext" object.

## Lessons Learned

- Do not invent rendering systems that don't exist in the original (water overlay, custom effects).
- The `_enum.h` enum values ARE the BMD action indices. Do NOT add offsets.
- Terrain dynamic lights use CPU-side lightmap grid modification, NOT per-pixel shader.
- RENDER_WAVE is ONLY for MODEL_MONSTER01+51, never for world objects.
- Terrain tile 255 = empty/invalid, fill with neutral dark brown (80,70,55) not magenta.
- Per-pixel point lights with large ranges create reddish spots -- use CPU-side lightmap instead.
- Item definitions use `defIndex = category * 32 + itemIndex` as the unique key across all systems.
- Client inventory is authoritative from server -- client sends requests, server validates and sends back results.
