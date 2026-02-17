# Build & Project Structure

## Build

```bash
cd build && cmake .. && make -j$(sysctl -n hw.ncpu)
```

Three targets: `MuRemaster` (world viewer), `ModelViewer` (BMD object browser), and `CharViewer` (character animation browser).
Dependencies: glfw3, GLEW, OpenGL, libjpeg-turbo (TurboJPEG), GLM (header-only), ImGui, giflib.

### Server Build
```bash
cd server/build && cmake .. && make -j$(sysctl -n hw.ncpu)
```

### macOS Specifics
- **Window Activation**: Uses `activateMacOSApp()` (Objective-C runtime) to force the GLFW window to the foreground on launch.
- **GLEW Header Order**: `GL/glew.h` **must** be included before `GLFW/glfw3.h` to prevent symbol conflicts.
- **Metal Translation Layer VBO Updates**: On macOS (OpenGL->Metal), `glBufferSubData` works for dynamic VBO updates. `glBufferData` + VAO re-setup does NOT work reliably. Always use `glBufferSubData` for animated mesh re-skinning.

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
| `HeroCharacter.hpp` | Player character: DK Naked model, click-to-move, skeletal walk animation, blob shadow, terrain snap, terrain lightmap sampling, point light uniforms. |
| `MonsterManager.hpp` | Monster system: multi-type monster rendering, server-driven state machine, combat animations, hover behavior. |
| `NetworkClient.hpp` | Client-side networking: connects to server, sends/receives packets. |
| `ClickEffect.hpp` | Click-to-move visual feedback: animated ring effect at click position on terrain. |
| `GrassRenderer.hpp` | Billboard grass system: wind animation, ball-push displacement, 3 texture layers. |
| `Sky.hpp` | Sky dome: gradient hemisphere rendered behind scene. |
| `FireEffect.hpp` | Particle-based fire system for Lorencia torches/bonfires/lights. Uses GPU instancing and billboarding. |
| `Screenshot.hpp` | `Screenshot::Capture(window)` for JPEG; GIF system for optimized frame-diffed animations. |

### Sources (src/)

| File | Purpose |
|------|---------|
| `BMDParser.cpp` | BMD decryption (XOR key + cumulative wKey) and binary parsing. |
| `BMDUtils.cpp` | Euler->quaternion->matrix, parent-chain bone concatenation, quaternion slerp interpolation. |
| `TextureLoader.cpp` | OZJ (JPEG+header) and OZT (TGA+header) loading with RLE, V-flip, extension resolution. |
| `Camera.cpp` | Camera math, smooth zoom lerp, state save/load to `camera_save.txt`. |
| `Terrain.cpp` | Terrain vertex grid generation, texture array loading, shader-based 4-tap blending. |
| `TerrainParser.cpp` | Decrypts and parses terrain files: heightmap, mapping, attributes, objects, lightmap. |
| `ObjectRenderer.cpp` | Loads BMD models by type ID, caches GPU meshes, renders 2870+ instances with per-mesh blend, BlendMesh glow, skeletal animation, terrain lightmap sampling. |
| `GrassRenderer.cpp` | 42k grass billboards with GPU vertex shader wind animation and ball-push displacement. |
| `HeroCharacter.cpp` | DK Naked character: 5-part body, skeletal animation, click-to-move, blob shadow with stencil buffer. |
| `MonsterManager.cpp` | Monster rendering, server-driven state machine (IDLE/WALKING/CHASING/ATTACKING/HIT/DYING/DEAD), hover for flying types. |
| `NetworkClient.cpp` | Packet serialization/deserialization, TCP socket management. |
| `main.cpp` | World viewer app: terrain + objects + grass + sky + fire + hero + monsters, click-to-move, combat, roof hiding. |
| `model_viewer_main.cpp` | Object browser: scans Object1/ for BMDs, orbit camera, ImGui. |
| `char_viewer_main.cpp` | Character browser: Player.bmd skeleton + body part armor system. |

### Shaders (shaders/)

| File | Purpose |
|------|---------|
| `model.vert` | Standard MVP transform, normal correction via inverse-transpose, `texCoordOffset` for UV scroll. |
| `model.frag` | Two-sided diffuse lighting, alpha discard 0.1, fog 1500-3500, `blendMeshLight`, `objectAlpha`, `terrainLight`, `luminosity`, point lights (64 max). **Must set `objectAlpha` to 1.0 or objects are invisible.** |
| `shadow.vert` | Minimal MVP vertex shader for blob shadow (position-only). |
| `shadow.frag` | Flat black at 30% opacity: `vec4(0, 0, 0, 0.3)`. |

## Data Paths

- Game assets: `references/other/MuMain/src/bin/Data/`
  - `Object1/` -- BMD models + OZT/OZJ textures
  - `Monster/` -- Monster BMD models + textures
  - `World1/` -- Lorencia terrain data
  - `Player/` -- Player body part BMDs
- Reference source: `references/other/Main5.2/Source Main 5.2/source/`
- Reference server data: `references/other/Main5.2/MuServer_Season_5_Update_15/Data/`

## Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| MAX_BONES | 200 | BMDStructs.hpp |
| TERRAIN_SIZE | 256 | TerrainParser.hpp |
| TERRAIN_SCALE | 100.0f | Terrain.cpp |
| Object record size | 30 bytes | TerrainParser.cpp |
| MAX_WORLD_OBJECTS | 160 | reference _enum.h |
| Alpha discard | 0.1 | shaders/model.frag |
| Fog range | 1500-3500 | shaders/model.frag |
| Game tick rate | 40ms (25 FPS) | Original engine ZzzScene.cpp |
