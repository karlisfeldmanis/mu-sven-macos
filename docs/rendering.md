# Rendering Pipeline

## Coordinate System
- MU Online uses Z-up right-handed: X-right, Y-forward, Z-up
- OpenGL uses Y-up right-handed: X-right, Y-up, Z-backward
- **Position mapping**: `GL = (MU_Y, MU_Z, MU_X)` -- cyclic permutation (det=1, no mirror)
- **Model geometry conversion**: `Rz(-90) * Ry(-90)` -- NOT Rx(-90)!
  - MU_X -> GL_Z (matches MU_X -> WorldZ position mapping)
  - MU_Y -> GL_X (matches MU_Y -> WorldX position mapping)
  - MU_Z -> GL_Y (height axis preserved)
- Terrain grid mapping: MU_Y (outer loop z) -> WorldX, MU_X (inner loop x) -> WorldZ
- World position = `(z * 100, height, x * 100)` where z/x are grid indices

## Per-Mesh Blend State
```
Global state: glEnable(GL_BLEND), glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

For each mesh:
  if hidden -> skip
  if noneBlend -> glDisable(GL_BLEND), draw, glEnable(GL_BLEND)
  if bright -> glBlendFunc(GL_ONE, GL_ONE) + glDepthMask(FALSE), draw, restore
  else -> normal draw (uses global alpha blend)
```

## Blob Shadow (ZzzBMD.cpp RenderBodyShadow)

Shadow projection formula in MU-local space:
```
pos.x += pos.z * (pos.x + 2000) / (pos.z - 4000)
pos.y += pos.z * (pos.y + 2000) / (pos.z - 4000)
pos.z = 5.0  // flatten to just above ground
```
- Facing rotation baked into vertices before projection
- Shadow model matrix: `translate(m_pos) * Rz(-90) * Ry(-90)` -- NO facing rotation
- Stencil buffer (`GL_EQUAL, 0` + `GL_INCR`) prevents overlap darkening
- Requires `glfwWindowHint(GLFW_STENCIL_BITS, 8)`
- Position-only VBOs (`GL_DYNAMIC_DRAW`), re-uploaded per frame via `glBufferSubData`

## World Object Rendering

Objects loaded from `EncTerrain1.obj`, parsed by `TerrainParser::ParseObjectsFile()`.

**Coordinate transform** (MU -> OpenGL):
- `WorldX = mu_pos[1]`, `WorldY = mu_pos[2]`, `WorldZ = mu_pos[0]`

**Per-instance model matrix**: translate -> Rz(-90) -> Ry(-90) -> MU_rotateZ/Y/X -> scale

**MU rotation convention** (AngleMatrix in ZzzMathLib.cpp):
- angles[0]=X (pitch), angles[1]=Y (yaw), angles[2]=Z (roll)
- Applied as Z*Y*X matrix
- Large angle values (e.g. 900) are valid -- sin/cos handle wrapping

**Model caching**: BMD models loaded once per type, shared across all instances. 109 unique models for ~2870 Lorencia instances.

## RENDER_WAVE -- NOT For World Objects

`RENDER_WAVE` (ZzzBMD.cpp:1331) is a procedural sine-wave vertex displacement. Formula: `pos += normal * sin((WorldTime_ms + vertexIndex * 931) * 0.007) * 28.0`. **ONLY** used for `MODEL_MONSTER01+51`, never for world objects.

## Roof Hiding System (objectAlpha)

When player stands on tile with `layer1 == 4` (building interior), types 125 (HouseWall05) and 126 (HouseWall06) fade to invisible. Reference: ZzzObject.cpp:3744.

- `objectAlpha` uniform in `model.frag`: multiplies fragment alpha
- Exponential ease: `alpha += (target - alpha) * (1 - exp(-20 * dt))`
- Objects with alpha < 0.01 skipped entirely

## EncTerrain.obj Format

Encrypted with same XOR key as other map files.
```
Header: Version(1) + MapNumber(1) + Count(2 bytes, short)
Per object (30 bytes each):
  Type:     short (2)    -- maps to BMD filename
  Position: vec3 float (12) -- MU world coords (TERRAIN_SCALE=100 scaled)
  Angle:    vec3 float (12) -- rotation in degrees
  Scale:    float (4)
```
Lorencia (World1): 2870 objects, 109 unique model types.

## Object Type-to-BMD Mapping

Follows `AccessModel()` convention (MapManager.cpp):
- `AccessModel(TYPE, "Data\\Object1\\", "BaseName", index)` -> `BaseName0X.bmd` or `BaseNameXX.bmd`

Key ranges: Tree(0-19), Grass(20-29), Stone(30-39), StoneStatue(40-42), Tomb(44-46), FireLight(50-51), Bonfire(52), DoungeonGate(55), SteelWall(65-67), StoneWall(69-74), StoneMuWall(75-78), Bridge(80), Fence(81-84), StreetLight(90), Cannon(91-93), Curtain(95), House(115-119), HouseWall(121-126), Furniture(140-146).

## GIF Capture Optimizations

- Resolution Downscaling: box-filter averaging during capture
- Frame Diffing: unchanged pixels marked transparent
- Dirty Rectangle Encoding: only changed bounding box per frame
- Frame Skipping: configurable capture frequency
