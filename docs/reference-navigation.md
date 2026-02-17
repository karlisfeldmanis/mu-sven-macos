# Reference Source Navigation

Source path: `references/other/Main5.2/Source Main 5.2/source/`
Server data: `references/other/Main5.2/MuServer_Season_5_Update_15/Data/`

## Key Source Files

| File | Key Functions | Purpose |
|------|--------------|---------|
| `ZzzBMD.cpp` | `Animation()` (line 55), `PlayAnimation()` (line 410), `RenderBody()` (line 1851), `RenderMesh()` (line 937) | BMD bone interpolation, frame advance, mesh rendering |
| `ZzzBMD.h` | structs: `BMD`, `Bone_t`, `BoneMatrix_t`, `Mesh_t`, `Action_t` | BMD data structures |
| `ZzzObject.cpp` | `OpenObjectsEnc()`, `CreateObject()`, `RenderObject()`, `RenderObjectMesh()` | World object loading, type setup, BlendMesh, rendering |
| `ZzzOpenData.cpp` | `OpenMonsterModel()` (line 2376), Player PlaySpeed (line 329) | Model loading, PlaySpeed tables for all types |
| `ZzzCharacter.cpp` | `CreateMonster()` (line 12577), `MoveCharacterPosition()` (line 6201), `MoveMonsterClient()` (line 6229), `CharacterAnimation()` (line 2280), `CharacterMoveSpeed()` (line 6114) | Monster type mapping, movement, hover, animation |
| `ZzzLodTerrain.cpp` | `RenderTerrainTile()` (line 1662), `FaceTexture()` (line 1447), `RenderTerrainFace()` | Terrain rendering, TW_NOGROUND skip, water UV |
| `ZzzAI.cpp` | `SetAction()` (line 444), `PathFinding2()` (line 727), `MovePath()` (line 609) | Client-side AI, A* pathfinding, Catmull-Rom movement |
| `ZzzScene.cpp` | `MainScene()` (line 2251) | 40ms tick loop, frame limiter |
| `ZzzMathLib.cpp` | `AngleQuaternion()`, `QuaternionMatrix()`, `R_ConcatTransforms()`, `AngleMatrix()` | Math utilities |
| `MapManager.cpp` | `AccessModel()` calls | Type-to-filename mapping |
| `_enum.h` | `MODEL_TREE01=0`, `MODEL_GRASS01=20`, `MODEL_CURTAIN01=95`, `PLAYER_SET=0` | Type and action enums |
| `_define.h` | `TERRAIN_SCALE=100`, `MONSTER01_STOP1=0..MONSTER01_DIE=6` | Game constants |
| `w_ObjectInfo.h` | `OBJECT` class | Position, Angle, Scale, Type, AnimationFrame, Velocity |
| `GlobalBitmap.cpp` | | Texture loading |
| `TextureScript.h/.cpp` | | Texture script flag parsing (_R/_H/_N) |

## Key Data Files

| File | Location | Content |
|------|----------|---------|
| `Monster.txt` | `MuServer.../Data/Monster/` | Monster stats (Level, HP, Damage, Defense, MoveSpeed, AtkSpeed, MvRange) |
| `MonsterSetBase.txt` | `MuServer.../Data/Monster/` | Spawn positions (MapNumber, Range, X, Y, Direction) |
| `DefaultClassInfo.txt` | `MuServer.../Data/` | Starting stats per class (STR, DEX, VIT, ENE, HP formula) |

## Architecture Notes

- **Monster AI is server-side only.** Client receives TargetX/TargetY and pathfinds there.
- **Animation is tick-based.** `AnimationFrame += PlaySpeed` per 40ms tick, NOT per second.
- **Movement is tick-based.** `Position += MoveSpeed` per tick, NOT delta-time scaled.
- **WorldTime** is `timeGetTime()` in milliseconds -- absolute, used for UV animations and flickers.
- **Default object Velocity** = 0.16 per tick = 4.0 keys/sec at 25fps.
