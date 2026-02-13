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

---

## Non-Character Tasks

### High Priority (Big Visual Impact)

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 1 | **Grass billboard rendering + wind animation** | `GrassRenderer.cpp` — 42k billboards, GPU wind shader, TileGrass OZT textures | Done |
| 2 | **Object blob shadows** | `ZzzObject.cpp` `RenderBodyShadow()` — perspective-project mesh to ground, fixed light dir `(-1, 0.03, -1)` | Pending |
| 3 | **Day/night luminosity cycle** | `ZzzAI.cpp` — `g_Luminosity = sin(WorldTime*0.004)*0.15+0.6`, range 0.45-0.75, ~26min cycle | Pending |
| 4 | **Per-object terrain light query** | `ZzzLodTerrain.cpp` `RequestTerrainLight()` — bilinear sample lightmap at object position → `BodyLight` color | Pending |

### Medium Priority

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 5 | **Subtractive blending mode** | `ZzzOpenglUtil.cpp` — `GL_ZERO, GL_ONE_MINUS_SRC_COLOR` | Pending |
| 6 | **Multiplicative blending mode** | `ZzzOpenglUtil.cpp` — `GL_ZERO, GL_SRC_COLOR` | Pending |
| 7 | **Dynamic terrain lighting from torches** | `ZzzLodTerrain.cpp` — per-frame terrain light modification near fire objects | Pending |
| 8 | **Water texture frame cycling** | `ZzzLodTerrain.cpp` — 32 pre-rendered water textures cycled per tick | Pending |
| 9 | **Missing ExtTile01-16 textures** | Terrain shader already handles ExtTiles, need to load actual texture files | Pending |

### Lower Priority

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 10 | **Fish in water areas** | `ZzzEffect.cpp` — animated sprites on water attribute tiles | Pending |
| 11 | **33 unmapped object types** | `MapManager.cpp` `AccessModel()` — extend `GetObjectBMDFilename()` type table | Pending |
| 12 | **Chrome/metal reflection mapping** | `ZzzOpenglUtil.cpp` — environment map for metallic surfaces | Pending |
| 13 | **Bloom/HDR post-processing** | Not in original — enhancement opportunity | Pending |
| 14 | **Weather system (rain)** | `GMRain.cpp` — particle rain with splash effects | Pending |

---

## Character-Related Tasks (Deferred)

| # | Task | Reference Files | Status |
|---|------|----------------|--------|
| 15 | **NPC placement + rendering** | `ZzzCharacter.cpp`, `GMNpcShop.cpp` — NPCs from server data | Pending |
| 16 | **NPC walk/idle AI** | `ZzzAI.cpp` `MoveMonsterVisual()` — patrol paths, idle animations | Pending |
| 17 | **Player character avatars** | `ZzzCharacter.cpp` — multi-part BMD assembly (body, helm, weapon) | Pending |
| 18 | **Stencil shadow volumes** | `ShadowVolume.cpp` — silhouette-based shadows via stencil buffer | Pending |
| 19 | **Character spell/skill effects** | `ZzzEffect.cpp` — particle trails, projectiles, auras | Pending |
| 20 | **Character lighting (BodyLight)** | `ZzzObject.cpp` — per-character terrain light + dynamic light sources | Pending |
| 21 | **Nameplates / UI overlays** | `GMInterface.cpp` — floating name tags above NPCs/players | Pending |
| 22 | **Character blend modes** | `ZzzOpenglUtil.cpp` — ghost/invisible effects, damage flash | Pending |
| 23 | **Pet / companion boids** | `GMPetSystem.cpp` — following AI + idle animations | Pending |

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
