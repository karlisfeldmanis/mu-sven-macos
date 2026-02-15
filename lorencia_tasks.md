
# Lorencia Map — Remaining Tasks

Reference source: `references/other/Main5.2/Source Main 5.2/source/`

## Completed

- [x] Terrain heightmap + tile blending (4-tap bilinear)
- [x] Lightmap integration (vertex color from TerrainLight.OZJ)
- [x] World object loading + rendering (2870 instances, 109 types)
- [x] BMD model parsing (version 0xC encryption, 0xA unencrypted)
- [x] Per-mesh blend state (_R bright, _H hidden, _N noneBlend)
- [x] BlendMesh / window light system (additive glow, flicker, UV scroll)
- [x] Skeletal animation (CPU retransform, slerp interpolation, 14 animated types)
- [x] Fire particle effects (GPU instanced billboards, Lorenzian torches/bonfires)
- [x] Point lights from world objects (terrain + object shader integration)
- [x] Water UV animation (horizontal scroll + sine wave in terrain shader)
- [x] Screenshot capture (JPEG + optimized GIF with frame diffing)
- [x] Grass billboard rendering + wind animation (42k billboards, GPU vertex shader wind, TileGrass OZT with alpha)
- [x] Per-object terrain light query (bilinear lightmap sampling at object position)
- [x] Sky dome rendering (gradient hemisphere behind scene)
- [x] Roof hiding (types 125/126 fade when hero on tile==4, exponential ease)
- [x] Grass pushing (billboard vertices pushed away from player with quadratic falloff)
- [x] Energy ball (player representation, terrain-following sphere with WASD movement)

---

## Non-Character Tasks

### High Priority (Big Visual Impact)

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 1 | **Object blob shadows** | `ZzzObject.cpp` `RenderBodyShadow()` — perspective-project mesh to ground, fixed light dir `(-1, 0.03, -1)` | Done |
| 2 | **Day/night luminosity cycle** | `ZzzAI.cpp` — `g_Luminosity = sin(WorldTime*0.004)*0.15+0.6`, range 0.45-0.75, ~26min cycle | Done |
| 3 | **StoneMuWall01 (type 75) UV scroll** | `ZzzObject.cpp:4250` — `BlendMeshTexCoordV = -(WorldTime%10000)*0.0002f` — slow flag cloth scroll on stone walls | Pending |
| 4 | **HouseEtc01 flag cloth animation (type 127)** | Not animated in original MU. Could add custom cloth sim or RENDER_WAVE-style procedural animation. BMD has 2 meshes: mesh 0 = pole (c_wall04), mesh 1 = flag cloth (c_wall06). 4 instances surround the fountain. | Future |

### Medium Priority

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 5 | **Waterspout particle emission** | `ZzzObject.cpp:2790` — `CreateParticle` from bones 1 and 4, water splash particles | Pending |
| 6 | **Water texture frame cycling** | `ZzzLodTerrain.cpp` — 32 pre-rendered water textures (`wt00-wt31.jpg`) — files not in data | Blocked |

### Lower Priority

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 7 | **Fish in water areas** | `ZzzEffect.cpp` — animated sprites on water attribute tiles | Pending |
| 8 | **Chrome/metal reflection mapping** | `ZzzOpenglUtil.cpp` — environment map for metallic surfaces | Pending |
| 9 | **Bloom/HDR post-processing** | Not in original — enhancement opportunity | Pending |
| 10 | **Weather system (rain)** | `GMRain.cpp` — particle rain with splash effects | Pending |

---

## Character-Related Tasks (Deferred)

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 11 | **NPC placement + rendering** | `ZzzCharacter.cpp`, `GMNpcShop.cpp` — NPCs from server data | Pending |
| 12 | **NPC walk/idle AI** | `ZzzAI.cpp` `MoveMonsterVisual()` — patrol paths, idle animations | Pending |
| 13 | **Player character avatars** | `ZzzCharacter.cpp` — multi-part BMD assembly (body, helm, weapon) | Pending |
| 14 | **Stencil shadow volumes** | `ShadowVolume.cpp` — silhouette-based shadows via stencil buffer | Pending |
| 15 | **Character spell/skill effects** | `ZzzEffect.cpp` — particle trails, projectiles, auras | Pending |
| 16 | **Character lighting (BodyLight)** | `ZzzObject.cpp` — per-character terrain light + dynamic light sources | Pending |
| 17 | **Nameplates / UI overlays** | `GMInterface.cpp` — floating name tags above NPCs/players | Pending |
| 18 | **Character blend modes** | `ZzzOpenglUtil.cpp` — ghost/invisible effects, damage flash | Pending |
| 19 | **Pet / companion boids** | `GMPetSystem.cpp` — following AI + idle animations | Pending |

---

## Research Notes

### Grass System (from ZzzLodTerrain.cpp)
- **Grass = billboard quads** rendered as a separate pass over terrain cells
- Identified by `TerrainFlag == TERRAIN_MAP_GRASS` (not an attribute bit)
- Only rendered when `TerrainMappingAlpha == 0` (no alpha overlay) and `CurrentLayer == 0`
- Wind displacement: `TerrainGrassWind[Index] = sin(WindSpeed + gridX * 5.0) * 10.0`
- WindSpeed: `(WorldTime % 720000) * 0.002` — 12-minute cycle
- Applied to vertex Y-axis of top two billboard corners
- Grass textures: `BITMAP_MAPGRASS + TerrainMappingLayer1[index]` (variant per cell)
- Random UV offset per row: `TerrainGrassTexture[yi] = rand()%4 / 4.0`
- Water also uses wind array for UV ripple: `TerrainGrassWind[index] * 0.002` on V-coord

### HouseEtc01-03 (Types 127-129) — Static Objects
- **HouseEtc01 (127)**: 2 meshes, 1 bone, 1 keyframe. Mesh 0 = pole (c_wall04.OZJ), mesh 1 = flag cloth (c_wall06.OZJ). Internal BMD name: "Data2\Object1\c_wall07.smd". Completely static in original MU — no animation, no RENDER_WAVE.
- **4 instances** surround the fountain at World≈(12800, 165, 14100).
- RENDER_WAVE (ZzzBMD.cpp:1331) is ONLY used for MODEL_MONSTER01+51 (a monster model), never for Lorencia world objects.

### Waterspout01 (Type 105) — Reference Behavior
- Skeletal animation + BlendMesh=3 (mesh 3 additive)
- BlendMeshLight = **1.0f constant** (not flicker). Reference: ZzzObject.cpp:2786
- UV scroll: `-(WorldTime%1000)*0.001f` on BlendMesh texture
- Particle emission from bones 1 and 4 (water splash effect)

### StoneMuWall01 (Type 75) — Flag UV Scroll
- `BlendMeshTexCoordV = -(int)WorldTime%10000 * 0.0002f` (ZzzObject.cpp:4250)
- Slow downward UV scroll on flag cloth texture
