# Terrain & Environment Systems

## Water Rendering (Terrain Shader)

The original MU engine for Lorencia renders water as a **regular tile** (layer1 index 5) with animated UV scrolling -- no overlay, no proximity kernel, no blue tint.

- `sampleLayerSmooth()` handles water tile animation: `tileUV.x += uTime * 0.1` + sinusoidal Y offset
- Shore transitions: handled naturally by alpha map blending between Layer1 and Layer2
- Bridge protection: `sampleLayerSmooth()` checks `TW_NOGROUND` (0x08) on bilinear neighbors
- TW_NOGROUND reconstruction: .att file lacks flags, reconstructed from bridge objects (type 80)

**Lesson learned**: Do not invent rendering systems that don't exist in the original source. For Lorencia, water is just a tile with animated UVs. Special water overlays only exist for Atlantis (WD_7ATLANSE).

## Terrain Tile Index 255

Layer1/layer2 may contain tile index 255 as "empty/invalid" marker. Fill unloaded texture slots with **neutral dark brown (80, 70, 55)** to blend with surrounding terrain. Magenta debug fill causes pink artifacts through bilinear blending.

## Terrain Lightmap on Objects

Objects sample the 256x256 RGB lightmap at their world position for ambient lighting.
- `ObjectRenderer::SetTerrainLightmap()` stores lightmap copy
- `ObjectRenderer::SampleTerrainLight()` bilinear-samples at world position
- `terrainLight` uniform in `model.frag` multiplies final lighting color

## Point Light System

64 point lights from fire emitter objects. Used differently for terrain vs objects:

**Terrain**: CPU-side `AddTerrainLight` matching original (ZzzLodTerrain.cpp). Each frame: reset lightmap, add lights to 256x256 grid cells with **linear falloff** and **cell range 3**, re-upload via `glTexSubImage2D`. Colors scaled x0.35.

**Objects/Characters**: Per-pixel in `model.frag` shader. Uniforms: `pointLightPos[64]`, `pointLightColor[64]`, `pointLightRange[64]`, `numPointLights`.

**Lesson learned**: Original MU applies dynamic lights to terrain via CPU-side lightmap grid modification, NOT per-pixel shader computation. Per-pixel with large ranges creates reddish spots.

## BlendMesh System (Window Light / Glow)

Marks specific mesh indices for additive blending (window glow, lamp light).

**Matching**: `Mesh_t::Texture` compared against per-type BlendMesh value. Match -> `mb.isWindowLight = true`.

**Per-type BlendMesh table**:
- 117 (House03) -> texID 4, 118 (House04) -> 8, 119 (House05) -> 2
- 122 (HouseWall02) -> 4, 52 (Bonfire01) -> 1, 90 (StreetLight01) -> 1
- 150 (Candle01) -> 1, 98 (Carriage01) -> 2, 105 (Waterspout01) -> 3

**BlendMeshLight intensity rules**:
- 117 (House03), 122 (HouseWall02): sin-based flicker (0.4-0.7)
- 118 (House04), 119 (House05): flicker + UV scroll
- 52 (Bonfire01): wider flicker (0.4-0.9)
- 90, 150, 98: constant 1.0 (no flicker)
- 105 (Waterspout01): constant 1.0 + UV scroll

## Fire System

- Data: `Data/Effect/Fire01.OZJ` (animated billboard strip)
- Fire types: 50-51 (FireLight), 52 (Bonfire), 55 (DungeonGate), 80 (Bridge), 130 (Light01)
- GPU Instancing: single `glDrawElementsInstanced` call
- Additive Blending: `GL_ONE, GL_ONE` with `glDepthMask(GL_FALSE)`

## Grass Pushing System

Grass billboard vertices near player pushed away (GMHellas.cpp:402 CheckGrass).
- Top vertices within `pushRadius` (150 units) of `ballPos` XZ displaced away
- Quadratic falloff + slight downward bend
- `ballPos` and `pushRadius` uniforms set per frame

## Luminosity System (Day/Night)

Uniform `luminosity` in `model.frag` and terrain shader. Currently disabled (defaults to 1.0).

**Original formula**: `g_Luminosity = sinf(WorldTime * 0.004f) * 0.15f + 0.6f` -- range 0.45-0.75, ~26 minute period. From ZzzAI.cpp.

## HouseEtc01-03 (Types 127-129)

HouseEtc01 has 2 meshes: pole + flag cloth. 1 bone, 1 keyframe. **Completely static in original MU**. 4 instances surround the fountain.
