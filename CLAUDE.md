# MU Online Remaster - Reference Library

Native C++20 restoration of the MU Online engine for macOS with OpenGL 3.3+.
This document helps AI agents understand the codebase without re-exploring.

## Build

```bash
cd build && cmake .. && make -j$(sysctl -n hw.ncpu)
```

Three targets: `MuRemaster` (world viewer), `ModelViewer` (BMD object browser), and `CharViewer` (character animation browser).
Dependencies: glfw3, GLEW, OpenGL, libjpeg-turbo (TurboJPEG), GLM (header-only), ImGui, giflib.

### macOS Specifics
- **Window Activation**: Uses `activateMacOSApp()` (Objective-C runtime) to force the GLFW window to the foreground on launch.
- **GLEW Header Order**: `GL/glew.h` **must** be included before `GLFW/glfw3.h` to prevent symbol conflicts.
- **Metal Translation Layer VBO Updates**: On macOS (OpenGL→Metal), `glBufferSubData` works for dynamic VBO updates. `glBufferData` + VAO re-setup does NOT work reliably. Always use `glBufferSubData` for animated mesh re-skinning.

## Source Files

### Headers (include/)

| File | Purpose |
|------|---------|
| `BMDStructs.hpp` | Data structures for BMD binary model format (Vertex_t, Normal_t, Triangle_t, Mesh_t, Bone_t, Action_t) |
| `BMDParser.hpp` | `BMDParser::Parse(path)` returns `unique_ptr<BMDData>`. Handles version 0xC encryption. |
| `BMDUtils.hpp` | Bone math: `ComputeBoneMatrices()`, `ComputeBoneMatricesInterpolated()` (slerp), `MuMath::TransformPoint/RotateVector`, AABB. Uses 3x4 row-major matrices. |
| `TextureLoader.hpp` | OZJ/OZT loading, texture resolution, `TextureScriptFlags` (_R/_H/_N suffixes), `TextureLoadResult` (ID + hasAlpha). |
| `MeshBuffers.hpp` | Per-mesh GPU state: VAO/VBO/EBO + rendering flags (hasAlpha, noneBlend, hidden, bright) + BlendMesh (bmdTextureId, isWindowLight) + animation (vertexCount, isDynamic). |
| `Shader.hpp` | RAII OpenGL shader wrapper. `use()`, `setMat4()`, `setVec3()`, `setBool()`. |
| `Camera.hpp` | FPS camera with yaw/pitch/zoom, WASD movement, state persistence. |
| `Terrain.hpp` | 256x256 heightmap mesh, 4-tap tile blending, lightmap integration. |
| `TerrainParser.hpp` | Parses MAP heightmaps, tile layers, alpha maps, attributes, objects, lightmaps. `TERRAIN_SIZE = 256`. `ObjectData` struct. |
| `ObjectRenderer.hpp` | World object rendering: BMD model cache, type-to-filename mapping, per-instance transforms, per-mesh blend state, skeletal animation (AnimState, RetransformMesh), BlendMesh system, terrain lightmap sampling per object, per-type alpha (roof hiding). |
| `ViewerCommon.hpp` | Shared viewer utilities: OrbitCamera, DebugAxes, UploadMeshWithBones/RetransformMeshWithBones helpers, ActivateMacOSApp, ImGui init. |
| `GrassRenderer.hpp` | Billboard grass system: wind animation, ball-push displacement, 3 texture layers. |
| `Sky.hpp` | Sky dome: gradient hemisphere rendered behind scene. |
| `FireEffect.hpp` | Particle-based fire system for Lorencia torches/bonfires/lights. Uses GPU instancing and billboarding. |
| `Screenshot.hpp` | `Screenshot::Capture(window)` for JPEG; GIF system for optimized frame-diffed animations. |

### Sources (src/)

| File | Purpose |
|------|---------|
| `BMDParser.cpp` | BMD decryption (XOR key + cumulative wKey) and binary parsing. See format spec below. |
| `BMDUtils.cpp` | Euler→quaternion→matrix, parent-chain bone concatenation, quaternion slerp interpolation. |
| `TextureLoader.cpp` | OZJ (JPEG+header) and OZT (TGA+header) loading with RLE, V-flip, extension resolution. |
| `Camera.cpp` | Camera math, smooth zoom lerp, state save/load to `camera_save.txt`. |
| `Terrain.cpp` | Terrain vertex grid generation, texture array loading, shader-based 4-tap blending. Water is rendered as regular tile (layer1=5) with animated UV — no overlay. |
| `Screenshot.cpp` | GIF optimization: resolution downscaling, frame diffing, and dirty rectangle encoding. |
| `TerrainParser.cpp` | Decrypts and parses terrain files: heightmap, mapping, attributes, objects (EncTerrain1.obj), lightmap. |
| `ObjectRenderer.cpp` | Loads BMD models by type ID, caches GPU meshes, renders 2870+ object instances with per-mesh blend, BlendMesh glow, skeletal animation (CPU re-skinning for whitelisted types), terrain lightmap sampling, per-type alpha for roof hiding. |
| `GrassRenderer.cpp` | 42k grass billboards with GPU vertex shader wind animation and ball-push displacement (quadratic falloff within pushRadius). |
| `Sky.cpp` | Sky dome gradient hemisphere. |
| `FireEffect.cpp` | Particle physics, emitter management, and instanced billboarding rendering for `Fire01.OZJ`. Fire types: 50-51, 52, 55, 80, 130. |
| `main.cpp` | World viewer app: terrain + objects + grass + sky + fire, WASD energy ball on terrain, P screenshot, roof hiding (tile==4), grass pushing. Data path: `references/other/MuMain/src/bin/Data/`. |
| `model_viewer_main.cpp` | Object browser: scans Object1/ for BMDs, orbit camera, ImGui list+info panel, per-mesh blend state, skeletal animation playback with ImGui controls. |
| `char_viewer_main.cpp` | Character browser: Player.bmd skeleton + body part armor system, class-aware skill/emote animation categories, orbit camera, GIF capture. |
| `ViewerCommon.cpp` | Shared viewer code: OrbitCamera math, DebugAxes rendering, bone-aware mesh upload/retransform, ImGui lifecycle. |

### Shaders (shaders/)

| File | Purpose |
|------|---------|
| `model.vert` | Standard MVP transform, normal correction via inverse-transpose, `texCoordOffset` for UV scroll. Inputs: aPos, aNormal, aTexCoord. |
| `model.frag` | Two-sided diffuse lighting (abs dot), alpha discard at 0.1, optional linear fog (1500-3500 range), `blendMeshLight` intensity, `objectAlpha` (per-instance roof hiding), `terrainLight` (lightmap color at object world position), point light array (64 max). **Any renderer using this shader MUST set `objectAlpha` uniform to 1.0 or objects will be invisible.** |

### GIF Capture Optimizations
- **Resolution Downscaling**: Optional scale factor (e.g., 0.5x) using box-filter averaging during capture.
- **Frame Diffing**: Consecutive frames are compared; unchanged pixels are marked with a transparent palette index (255).
- **Dirty Rectangle Encoding**: Only the bounding box of changed pixels is stored in each frame (via `EGifPutImageDesc`).
- **Frame Skipping**: Configurable capture frequency to reduce memory footprint.

## BMD Binary Format

### Versions & Encryption
- **Versions**: Supports **0xC** (encrypted) and **0xA** (unencrypted).
- **Encryption (version 0xC)**:
  - XOR key (16 bytes, cycling): `[0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2]`
  - Cumulative key: initial `wKey = 0x5E`, per byte: `wKey = (src[i] + 0x3D)`
  - Formula: `dst[i] = (src[i] ^ xorKey[i % 16]) - wKey`

### File Layout
```
"BMD" + version(1)
if version == 0xC: encryptedSize(4) + encryptedPayload(...)

Decrypted payload:
  Name(32)
  NumMeshes(short) NumBones(short) NumActions(short)

  Per mesh:
    NumVertices(short) NumNormals(short) NumTexCoords(short) NumTriangles(short) TextureID(short)
    Vertices:  count * 16 bytes  [Node(2) + Pad(2) + Position(12)]
    Normals:   count * 20 bytes  [Node(2) + Pad(2) + Normal(12) + BindVertex(2) + Pad(2)]
    TexCoords: count * 8 bytes   [U(float) + V(float)]
    Triangles: count * 64 bytes  [see Triangle_t below]
    TextureName(32)

  Per action:
    NumAnimationKeys(short) LockPositions(bool)
    if locked: Positions(count * 12)

  Per bone:
    Dummy(bool)
    if not dummy: Name(32) Parent(short) then per-action: Position(keys*12) Rotation(keys*12)
```

### Triangle_t On-Disk Layout (64 bytes per triangle)
The file stores Triangle_t2 format (MSVC struct with padding). We read the first 34 meaningful bytes:
```
offset 0:  char Polygon        (1 byte, value 3=tri or 4=quad)
offset 1:  [padding]           (1 byte)
offset 2:  short VertexIndex[4]    (8 bytes)
offset 10: short NormalIndex[4]    (8 bytes)
offset 18: short TexCoordIndex[4]  (8 bytes)
offset 26: [LightMapCoord data]   (we read into EdgeTriangleIndex, unused)
offset 34: [more padding/lightmap] (we skip)
```
**Important**: We `memcpy` 34 bytes then advance ptr by 64. The fields at offsets 0-25 contain the actual vertex/normal/texcoord indices. Bytes 26+ are lightmap data from Triangle_t2 that we discard. Failure to use the 64-byte stride leads to alignment shifts and `std::length_error` during parsing.

## Fire & Atmospheric Effects

### Fire System (FireEffect.cpp)
- **Data**: Uses `Data/Effect/Fire01.OZJ` (animated billboard strip).
- **Emitters**: Spatially placed based on BMD object type (e.g., fountain, torch, light).
- **Fire types**: 50-51 (FireLight), 52 (Bonfire), 55 (DungeonGate), 80 (Bridge), 130 (Light01).
- **Rendering**:
  - **GPU Instancing**: All particles drawn in a single call via `glDrawElementsInstanced`.
  - **Billboarding**: Billboard matrix reconstructed in shader to face camera.
  - **Additive Blending**: `GL_ONE, GL_ONE` with `glDepthMask(GL_FALSE)`.

### BMD Skeletal Animation System
The original MU engine animates cloth, signs, and decorative objects using bone-based skeletal animation.

**Data flow**: BMD files → per-bone keyframes (Position + Rotation per action) → `ComputeBoneMatricesInterpolated()` with quaternion slerp → CPU vertex retransformation → `glBufferSubData` to `GL_DYNAMIC_DRAW` VBOs.

**Quaternion pre-computation**: At BMD parse time, Euler rotation keyframes are converted to quaternions via `MuMath::AngleQuaternion()` and stored in `BoneMatrix_t::Quaternion` for efficient slerp at runtime.

**Animation speed**: Reference uses `Velocity = 0.16f` per game tick at ~25fps = **4.0 keyframes/sec**. Implemented as `ANIM_SPEED = 4.0f` in `ObjectRenderer::Render()`.

**Animation whitelist** (CPU re-skinning enabled): Types 56-57 (MerchantAnimal), 59 (TreasureChest), 60 (Ship), 90 (StreetLight), 95 (Curtain), 96 (Sign), 98 (Carriage), 105 (Waterspout), 110 (Hanging), 118-119 (House04-05), 120 (Tent), 150 (Candle). Trees (0-19) and stone walls (72-74) have animation data but are excluded due to high instance count (~hundreds).

**Key functions**:
- `ComputeBoneMatricesInterpolated(bmd, action, frame)` — slerp between keyframes
- `ObjectRenderer::RetransformMesh()` — re-skin vertices with new bone matrices
- `AnimState` per model type — tracks float frame counter, shared across instances

### BlendMesh System (Window Light / Glow)
Original MU's BlendMesh marks specific mesh indices within BMD models for additive blending to simulate window glow, lamp light, and fire effects.

**Matching**: `Mesh_t::Texture` (BMD texture slot ID) is compared against a per-type BlendMesh value. Matching meshes get `mb.isWindowLight = true`.

**Per-type BlendMesh table** (in `GetBlendMeshTexId()`):
- 117 (House03) → texID 4, 118 (House04) → 8, 119 (House05) → 2
- 122 (HouseWall02) → 4, 52 (Bonfire01) → 1, 90 (StreetLight01) → 1
- 150 (Candle01) → 1, 98 (Carriage01) → 2, 105 (Waterspout01) → 3

**Rendering**: Additive blend (`GL_ONE, GL_ONE`) + depth write off + intensity flicker (sin-based) + optional UV scroll (House04, House05, Waterspout01).

**BlendMeshLight intensity rules** (from reference ZzzObject.cpp):
- 117 (House03), 122 (HouseWall02): sin-based flicker (0.4-0.7 range)
- 118 (House04), 119 (House05): flicker + UV scroll `-(WorldTime%1000)*0.001f`
- 52 (Bonfire01): wider flicker (0.4-0.9 range)
- 90 (StreetLight01), 150 (Candle01), 98 (Carriage01): **constant 1.0f** (no flicker)
- 105 (Waterspout01): **constant 1.0f** + UV scroll `-(WorldTime%1000)*0.001f`

**Shader uniforms**: `blendMeshLight` (float intensity multiplier), `texCoordOffset` (vec2 UV scroll).

### Point Light System
64 point lights collected from world objects (fire emitter types) and passed as uniform arrays to both terrain and model shaders.

**Shader uniforms**: `pointLightPos[64]`, `pointLightColor[64]`, `pointLightRange[64]`, `numPointLights`.

**Light templates**: Per object type, defines color (warm orange for torches, yellow for street lamps) and range. Collected in `main.cpp` from fire emitter object instances.

### Water Rendering (Terrain Shader)
The original MU engine for standard maps (Lorencia) renders water as a **regular tile** (layer1 index 5) with animated UV scrolling — no overlay, no proximity kernel, no blue tint. Our implementation matches this:

- **`sampleLayerSmooth()`** handles water tile animation: `tileUV.x += uTime * 0.1` + sinusoidal Y offset
- **Shore transitions**: Handled naturally by the alpha map blending between Layer1 and Layer2
- **Bridge protection**: `sampleLayerSmooth()` checks `TW_NOGROUND` (0x08) on bilinear neighbors; at bridge boundaries, snaps to center cell texture to prevent water bleeding onto road
- **No water surface overlay**: A custom water overlay (5x5 proximity kernel + blue tint + roadSuppression) was previously added but caused bridge artifacts. Removed — the original engine doesn't have it for Lorencia.
- **TW_NOGROUND reconstruction**: The .att file for our data version lacks these flags. Reconstructed in `main.cpp` from bridge objects (type 80) with orientation-aware rectangular footprint + 1-cell expansion to adjacent water cells.

**Lesson learned**: Do not invent rendering systems that don't exist in the original source. For Lorencia, water is just a tile with animated UVs. The original `RenderTerrainTile()` simply skips TW_NOGROUND cells entirely (line 1665 of ZzzLodTerrain.cpp). Special water overlays only exist for Atlantis (WD_7ATLANSE) in the original.

### Roof Hiding System (objectAlpha)
When the player stands on a tile with `layer1 == 4` (building interior), types 125 (HouseWall05) and 126 (HouseWall06) fade to invisible. Reference: ZzzObject.cpp:3744.

- **`objectAlpha` uniform** in `model.frag`: multiplies fragment alpha (0=invisible, 1=opaque)
- **`ObjectRenderer::SetTypeAlpha()`** accepts per-type alpha map
- **Interpolation**: Exponential ease `alpha += (target - alpha) * (1 - exp(-20 * dt))` in `main.cpp`
- **Skip rendering**: Objects with alpha < 0.01 are skipped entirely

### Grass Pushing System
Grass billboard vertices near the player ball get pushed away. Reference: GMHellas.cpp:402 `CheckGrass`.

- Implemented in GrassRenderer vertex shader: top vertices (`aWindWeight > 0`) within `pushRadius` (150 units) of `ballPos` XZ are displaced away with quadratic falloff + slight downward bend.
- `ballPos` and `pushRadius` uniforms set per frame from `main.cpp`.

### RENDER_WAVE — Not For World Objects
`RENDER_WAVE` (ZzzBMD.cpp:1331) is a procedural sine-wave vertex displacement along normals. Formula: `pos += normal * sin((WorldTime_ms + vertexIndex * 931) * 0.007) * 28.0`. It is **ONLY** used for `MODEL_MONSTER01+51` (a monster model), **never** for any Lorencia world objects. Do not apply it to world objects.

### HouseEtc01-03 (Types 127-129) — Static Objects
HouseEtc01 (type 127) has 2 meshes: mesh 0 = pole (c_wall04.OZJ), mesh 1 = flag cloth (c_wall06.OZJ). Internal BMD name: "Data2\Object1\c_wall07.smd". 1 bone, 1 keyframe. **Completely static in original MU** — no animation of any kind. 4 instances surround the fountain in Lorencia.

### Terrain Lightmap on Objects
Objects sample the terrain lightmap at their world position for ambient lighting, matching the original engine's per-object lighting.

- **`ObjectRenderer::SetTerrainLightmap()`** stores a copy of the 256x256 RGB lightmap
- **`ObjectRenderer::SampleTerrainLight()`** bilinear-samples the lightmap at object world position
- **`terrainLight` uniform** in `model.frag` multiplies the final lighting color

### Character Viewer & Player.bmd Action Indices
The character viewer (`char_viewer_main.cpp`) loads `Player.bmd` (60 bones, 0 meshes, **284 actions**) as a skeleton, then loads class-specific body part BMDs (Helm, Armor, Pants, Gloves, Boots) and re-skins them with the skeleton's bone transforms.

**Player.bmd action indices** are sequential enum values from `_enum.h` (0.97d version, `YDG_ADD_SKILL_RIDING_ANIMATIONS` ifdef is NOT defined). Enum values map **1:1 to BMD action indices** — no offset needed. `MAX_PLAYER_ACTION = 284`.

**Key action index ranges** (from `_enum.h`, PLAYER_SET=0):
- Idle: 1-14 (Stop Male/Female/weapon variants, Fly, Ride)
- Walk: 15-24 (Male/Female/weapon variants, Swim)
- Run: 25-37 (weapon variants, Fly, Ride)
- Combat: 38-59 (Fist, Sword, Bow, Ride attacks)
- DK Sword Skills: 60-64 (PLAYER_ATTACK_SKILL_SWORD1-5), 65 (Wheel), 66 (Fury Strike)
- Heal: 67 (PLAYER_SKILL_VITALITY)
- Spear/DeathStab: 70-71
- DarkLord/Fenrir: 74-129 (present in BMD but not used in 0.97d viewer)
- Magic Skills: **146** (Energy Ball/HAND1), 147 (HAND2), 148 (WEAPON1), 149 (WEAPON2), 150 (ELF1), **151** (Teleport), 152 (Flash/Aqua Beam), **153** (Inferno), **154** (Hell Fire)
- Emotes: **186** (Defense), 187-198 (Greeting→Gesture m/f pairs), 201-216 (Cry→Again m/f), 217-221 (Respect→Paper)
- Other: **230** (Shock), 231-232 (Die), 233-240 (Sit/Healing/Pose m/f)

**Lesson learned**: The `_enum.h` enum values are the BMD action indices. Do NOT add offsets. The `YDG_ADD_SKILL_RIDING_ANIMATIONS` ifdef is never `#define`d in any header — those 6 extra entries do NOT exist in the Player.bmd (confirmed: 284 actions = MAX_PLAYER_ACTION without ifdef).

### Reference Code Navigation
Key functions in the original MU source for future lookups:
- **ZzzObject.cpp**: `OpenObjectsEnc()` (load world objects), `CreateObject()` (per-type setup, BlendMesh assignment), `RenderObject()` / `RenderObjectMesh()` (draw with blend state)
- **ZzzBMD.cpp**: `Animation()` (frame interpolation, slerp), `RenderBody()` / `RenderMesh()` (bone transform + draw)
- **ZzzLodTerrain.cpp**: `RenderTerrainTile()` (line 1662, TW_NOGROUND skip at 1665), `FaceTexture()` (line 1447, water UV animation via WaterMove), `RenderTerrainFace()` (base terrain pass)
- **ZzzMathLib.cpp**: `AngleQuaternion()`, `QuaternionMatrix()`, `R_ConcatTransforms()`, `AngleMatrix()`
- **MapManager.cpp**: `AccessModel()` calls define type-to-filename mapping
- **_enum.h**: `MODEL_TREE01=0`, `MODEL_GRASS01=20`, `MODEL_CURTAIN01=95`, etc.
- **w_ObjectInfo.h**: OBJECT class with Position, Angle, Scale, Type, AnimationFrame, Velocity

## OZJ Format (JPEG variant)

- Optional 24-byte MU header before JPEG data
- Detect JPEG magic `0xFF 0xD8` at offset 0 or 24
- Decompress with TurboJPEG to RGB (no BOTTOMUP flag - keep top-to-bottom)
- MU uses DirectX UV convention (V=0 = top of image); native JPEG order matches this in OpenGL

## OZT Format (TGA variant)

- Optional 4 or 24-byte MU header before TGA header
- Detect TGA by checking `imageType` (byte 2) for value 2 or 10 at offset+2
- **Do NOT use TGA idLength** (byte 0) — MU OZT files don't follow standard TGA ID convention. Pixel data always starts at `offset + 18`.
- Supports type 2 (uncompressed) and type 10 (RLE compressed)
- Supports 24-bit (BGR) and 32-bit (BGRA, has alpha for transparency)
- **Always V-flip** rows: MU stores data bottom-to-top (standard TGA), flip to top-to-bottom so v=0 maps to top of image (matching DirectX UV convention in OpenGL)

### Texture Resolution Priority
When BMD references `texture.tga` but actual files are `.OZT`/`.OZJ`:
- If original extension is `.tga`/`.ozt`/`.bmp` → try TGA variants first (preserves alpha)
- If original extension is `.jpg`/`.ozj` → try JPEG variants first
- This prevents loading JPEG (no alpha) when TGA (with alpha) is needed for transparency

## Texture Script Flags
Parsed from texture filenames: `basename_FLAGS.ext` where FLAGS = combination of R, H, N, S.
- `_R` → bright (additive blend: `GL_ONE, GL_ONE` + depth write off)
- `_H` → hidden (skip rendering entirely)
- `_N` → noneBlend (disable blending, render opaque)
- Only valid if ALL characters after last `_` are recognized flags

## Rendering Pipeline

### Per-Mesh Blend State (model_viewer_main.cpp RenderScene)
```
Global state: glEnable(GL_BLEND), glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

For each mesh:
  if hidden → skip
  if noneBlend → glDisable(GL_BLEND), draw, glEnable(GL_BLEND)
  if bright → glBlendFunc(GL_ONE, GL_ONE) + glDepthMask(FALSE), draw, restore
  else → normal draw (uses global alpha blend)
```

### Coordinate System
- MU Online uses Z-up right-handed: X-right, Y-forward, Z-up
- OpenGL uses Y-up right-handed: X-right, Y-up, Z-backward
- **Position mapping**: `GL = (MU_Y, MU_Z, MU_X)` — this is a cyclic permutation (det=1, no mirror)
- **Model geometry conversion**: `Rz(-90°) * Ry(-90°)` — NOT Rx(-90°)! Must match position mapping.
  - MU_X → GL_Z (matches MU_X → WorldZ position mapping)
  - MU_Y → GL_X (matches MU_Y → WorldX position mapping)
  - MU_Z → GL_Y (height axis preserved)
- AABB center conversion: `(x, y, z)` → `(x, z, -y)` for orbit camera target (model viewer only)
- Terrain grid mapping: MU_Y (outer loop z) → WorldX, MU_X (inner loop x) → WorldZ
- World position = `(z * 100, height, x * 100)` where z/x are grid indices

### World Object Rendering (ObjectRenderer)
Objects are loaded from `EncTerrain1.obj` (encrypted binary), parsed by `TerrainParser::ParseObjectsFile()`.

**Coordinate transform** (MU → OpenGL):
- `WorldX = mu_pos[1]` (MU Y → WorldX)
- `WorldY = mu_pos[2]` (MU Z/height → WorldY)
- `WorldZ = mu_pos[0]` (MU X → WorldZ)

**Per-instance model matrix**: translate → Rz(-90°) → Ry(-90°) → MU_rotateZ/Y/X → scale

**MU rotation convention** (from reference `AngleMatrix` in ZzzMathLib.cpp):
- angles[0]=X (pitch), angles[1]=Y (yaw), angles[2]=Z (roll)
- Applied as Z*Y*X matrix (vertex sees X first, then Y, then Z)
- Angles stored in degrees in .obj file, converted to radians in TerrainParser
- Large angle values (e.g. 900°) are valid — sin/cos handle wrapping

**Model caching**: BMD models loaded once per type, GPU meshes shared across all instances of that type. 109 unique models for ~2870 Lorencia instances.

## EncTerrain.obj Format (World Object Placement)

Encrypted with same XOR key as other map files (`DecryptMapFile()`).

```
Header: Version(1) + MapNumber(1) + Count(2 bytes, short)
Per object (30 bytes each):
  Type:     short (2)    — maps to BMD filename via GetObjectBMDFilename()
  Position: vec3 float (12) — MU world coords (already TERRAIN_SCALE=100 scaled)
  Angle:    vec3 float (12) — rotation in degrees
  Scale:    float (4)
```

Lorencia (World1): 2870 objects, 109 unique model types.

### Object Type-to-BMD Mapping

Follows original engine's `AccessModel()` convention (MapManager.cpp):
- `AccessModel(TYPE, "Data\\Object1\\", "BaseName", index)` → `BaseName0X.bmd` or `BaseNameXX.bmd`
- Implemented in `ObjectRenderer::GetObjectBMDFilename(int type)`

Key ranges: Tree(0-19), Grass(20-29), Stone(30-39), StoneStatue(40-42), Tomb(44-46), FireLight(50-51), Bonfire(52), DoungeonGate(55), SteelWall(65-67), StoneWall(69-74), StoneMuWall(75-78), Bridge(80), Fence(81-84), StreetLight(90), Cannon(91-93), Curtain(95), House(115-119), HouseWall(121-126), Furniture(140-146), and more. Full table in ObjectRenderer.cpp.

## Data Paths
- Game assets: `references/other/MuMain/src/bin/Data/`
  - `Object1/` — BMD models + OZT/OZJ textures
  - `World1/` — Lorencia terrain data
- Reference source: `references/other/Main5.2/Source Main 5.2/source/`
  - Key files: `ZzzBMD.h/.cpp` (BMD format), `GlobalBitmap.cpp` (texture loading), `TextureScript.h/.cpp` (script flags)
  - Object placement: `ZzzObject.cpp` (OpenObjectsEnc, CreateObject, RenderObject), `MapManager.cpp` (AccessModel calls, type-to-filename)
  - Enums: `_enum.h` (MODEL_TREE01=0, MODEL_GRASS01=20, etc.), `_define.h` (TERRAIN_SCALE=100)
  - Object struct: `w_ObjectInfo.h` (OBJECT class: Position, Angle, Scale, Type)
- Reference tools: `references/other/MuClientTools16/_src_/Core/`

## Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| MAX_BONES | 200 | BMDStructs.hpp |
| TERRAIN_SIZE | 256 | TerrainParser.hpp |
| TERRAIN_SCALE | 100.0f | Terrain.cpp (world units per grid cell) |
| Object record size | 30 bytes | TerrainParser.cpp (Type+Position+Angle+Scale) |
| MAX_WORLD_OBJECTS | 160 | reference _enum.h (type range 0-159) |
| Vertex size | 16 bytes | BMDParser.cpp:111 |
| Normal size | 20 bytes | BMDParser.cpp:117 |
| TexCoord size | 8 bytes | BMDParser.cpp:121 |
| Triangle stride | 64 bytes | BMDParser.cpp:128 |
| Triangle read | 34 bytes | BMDParser.cpp:126 |
| Alpha discard | 0.1 | shaders/model.frag |
| Fog range | 1500-3500 | shaders/model.frag |
