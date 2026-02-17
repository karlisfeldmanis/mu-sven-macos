# MU Online Remaster - Reference Library

Native C++20 restoration of the MU Online engine for macOS with OpenGL 3.3+.
Reference source: Main 5.2. We are **migrating, not innovating** -- stick to the original source as the source of truth.

## Build

```bash
cd build && cmake .. && make -j$(sysctl -n hw.ncpu)
cd server/build && cmake .. && make -j$(sysctl -n hw.ncpu)
```

## Documentation Index

Detailed reference docs are in `docs/`:

| Document | Content |
|----------|---------|
| [docs/build-and-project.md](docs/build-and-project.md) | Build system, source file index, data paths, key constants |
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

## Lessons Learned

- Do not invent rendering systems that don't exist in the original (water overlay, custom effects).
- The `_enum.h` enum values ARE the BMD action indices. Do NOT add offsets.
- Terrain dynamic lights use CPU-side lightmap grid modification, NOT per-pixel shader.
- RENDER_WAVE is ONLY for MODEL_MONSTER01+51, never for world objects.
- Terrain tile 255 = empty/invalid, fill with neutral dark brown (80,70,55) not magenta.
- Per-pixel point lights with large ranges create reddish spots -- use CPU-side lightmap instead.
