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
- **VFXManager** -- Particle bursts, ribbon/lightning trails, fire/meteor effects (Main 5.2 1:1)
- **MonsterManager** -- Monster rendering, weapons, arrow projectiles, debris, nameplates
- **HeroCharacter** -- Player model, weapon attachment (safe zone back / combat hand), stat formulas

Server uses handler-based architecture with SQLite persistence:
- **PacketHandler** routes to **CharacterHandler**, **CombatHandler**, **InventoryHandler**, **WorldHandler**, **ShopHandler**
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

## Database

Canonical path: `server_build/mu_server.db` (auto-created on first server run via `SeedItemDefinitions()` + `SeedNpcSpawns()`). Delete to reset/re-seed. Schema: `characters`, `character_inventory`, `character_equipment`, `character_skills`, `item_definitions`, `npc_spawns`.

## AG (Ability Gauge) System

DK does NOT use Mana — uses AG (Ability Gauge / Stamina) for skill costs.
- **Formula**: `MaxAG = 1.0*ENE + 0.3*VIT + 0.2*DEX + 0.15*STR` (OpenMU ClassDarkKnight.cs)
- Server repurposes mana packet fields for AG when class is DK
- Client shows "AG" label instead of "Mana" in HUD and character panel
- AG recovers at 5% of max per second (server-side tick in GameWorld)

## Skill System

Skills are server-authoritative. Stored in `character_skills` table. DK starts with 0 skills, learns all via skill orbs (category 12 items). Packet: `SKILL_LIST` (0x41, C2 variable-length).

### DK 0.97d Skills

| Skill | ID | AG Cost | Level Req | Orb Index | Description |
|-------|----|---------|-----------|-----------|-------------|
| Falling Slash | 19 | 9 | 1 | 20 | Downward slash |
| Lunge | 20 | 9 | 1 | 21 | Forward thrust |
| Uppercut | 21 | 8 | 1 | 22 | Upward strike |
| Cyclone | 22 | 9 | 1 | 23 | Spinning attack |
| Slash | 23 | 10 | 1 | 24 | Horizontal slash |
| Twisting Slash | 41 | 10 | 30 | 7 | AoE spinning slash |
| Rageful Blow | 42 | 20 | 170 | 12 | Powerful ground strike |
| Death Stab | 43 | 12 | 160 | 19 | Piercing stab |

### Skill Icons

`Data/Interface/Skill.OZJ` — 512x512 sprite sheet, 25 icons/row, each 20x28px. Icon index = skill ID.

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
- **Weapon attachment**: `parentMat = charBone[attachBone] * offset`, then `wFinalBones[i] = parentMat * weaponLocalBone[i]`, then re-skin vertices. Weapon mesh upload MUST use `isDynamic=true` or `RetransformMeshWithBones` is a no-op.
- **BlendMesh in Main 5.2**: `BlendMesh=N` means mesh with `Texture==N` renders additive, other meshes render normally. It is NOT "render only mesh N". Arrow uses BlendMesh=1 (mesh 1 = fire glow additive, mesh 0 = shaft normal).
- **Skill model textures**: Models in `Data/Skill/` reference textures in that same directory. When loading via `loadMonsterModel`, pass `texDirOverride` pointing to `Data/Skill/` or textures won't resolve.
- **Bone positions are model-local**: `cachedBones` from `ComputeBoneMatrices` are in BMD-local space. To get world positions, apply model rotation matrix: `rotate(-90°Z) * rotate(-90°Y) * rotate(facing)` then scale + translate.
- **Giant (type 7) has no blood**: Main 5.2 excludes MODEL_MONSTER01+7 from blood particle spawning.
- **NPC direction values**: DB stores OpenMU-style 1-8 (West=1, SouthWest=2, South=3, SouthEast=4, East=5, NE=6, North=7, NW=8). Main 5.2 MonsterSetBase uses raw protocol 0-7 (add +1 for our DB). Client formula: `facing = (dir-1) * PI/4`.
- **NPC coordinates are version-stable**: Lorencia NPC positions (grid x/y) are identical across Version075, 095d, Season 6, and Main 5.2. Season 6 inherits from 095d which inherits from 075.
