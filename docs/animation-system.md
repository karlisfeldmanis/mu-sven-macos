# Animation System

Reference: ZzzBMD.cpp, ZzzOpenData.cpp, ZzzCharacter.cpp

## Tick-Based Timing

The original MU engine runs at **25 FPS target** (40ms per tick). Each tick calls `PlayAnimation()` once, advancing `AnimationFrame += PlaySpeed`. Animation is NOT delta-time scaled in the original.

```
// ZzzScene.cpp main loop
for (Remain = TimeRemain; Remain >= 40; Remain -= 40) {
    MoveMainScene();  // calls MoveCharacters, MoveObjects, etc.
}
// Sleep if frame took less than 40ms
```

`WorldTime = timeGetTime()` in milliseconds (absolute wall-clock). Used for UV animations, light flickers, etc.

## PlayAnimation() Core Loop (ZzzBMD.cpp:410)

```cpp
*AnimationFrame += Speed;  // Speed = PlaySpeed, called once per tick
// If Loop=true: clamp at NumAnimationKeys-1 (stops)
// If Loop=false: wrap modulo NumAnimationKeys (repeats)
```

## Animation() Bone Interpolation (ZzzBMD.cpp:55)

- `s1 = frac(AnimationFrame)` = blend weight toward current keyframe
- `s2 = 1 - s1` = weight toward prior keyframe
- Rotations: quaternion slerp between `PriorAction[PriorFrame]` and `CurrentAction[CurrentFrame]`
- Positions: linear interpolation with same weights
- `LockPositions`: root bone X/Y locked to frame 0 (locomotion root motion)
- `BodyHeight` added to root Z when positions are locked

Quaternions are pre-computed at BMD parse time from Euler rotation keyframes via `AngleQuaternion()`.

## Monster Animation Speeds (ZzzOpenData.cpp:2413)

Default PlaySpeed for ALL monster types (per tick at 25fps):

| Action | PlaySpeed | Keys/sec |
|--------|-----------|----------|
| STOP1 | 0.25 | 6.25 |
| STOP2 | 0.20 | 5.0 |
| WALK | 0.34 | 8.5 |
| ATTACK1 | 0.33 | 8.25 |
| ATTACK2 | 0.33 | 8.25 |
| SHOCK | 0.50 | 12.5 |
| DIE | 0.55 (Loop=true) | 13.75 |

### Global Type Multipliers (applied to STOP1..SHOCK, NOT to DIE)

| Type | Multiplier | Monster |
|------|-----------|---------|
| 3 (Spider BMD) | 1.2x | Spider |
| 5, 25 | 0.7x | Hell Hound, Ice Queen |
| 37, 42 | 0.4x | Devil, Red Dragon |

### Per-Type Walk Overrides

| BMD Type | Walk PlaySpeed | Monster |
|----------|---------------|---------|
| 2 | 0.7 | Budge Dragon |
| 6 | 0.6 | Lich |
| 8 | 0.7 | Poison Bull Fighter |
| 9 | 1.2 | Thunder Lich |
| 10 | 0.28 | Dark Knight |
| 12 | 0.3 | Larva |
| 13 | 0.28 | Hell Spider |
| 17 | 0.5 | Cyclops |
| 19 | 0.6 | Yeti |
| 20 | 0.4 | Elite Yeti |

### Lorencia Monster Summary (after multipliers)

| Server Type | Name | BMD Type | STOP1 | WALK | ATTACK | SHOCK | DIE |
|-------------|------|----------|-------|------|--------|-------|-----|
| 0 | Bull Fighter | 0 | 0.25 | 0.34 | 0.33 | 0.50 | 0.55 |
| 1 | Hound | 1 | 0.25 | 0.34 | 0.33 | 0.50 | 0.55 |
| 2 | Budge Dragon | 2 | 0.25 | **0.70** | 0.33 | 0.50 | 0.55 |
| 3 | Spider | 9 | 0.30 | **1.2*0.34=0.408** | 0.396 | 0.60 | 0.55 |

**Note for Spider (BMD type 3):** The 1.2x global multiplier applies to STOP1..SHOCK (not DIE). So Spider STOP1=0.30, WALK=0.34*1.2=0.408 (but then overridden to... no, wait: the walk override for type 9 is `1.2` as a direct value, not the multiplier result). Actually re-reading the source:
- Global multiplier: `if(Type==3) Actions[j].PlaySpeed *= 1.2f;` applies to ALL actions STOP1..SHOCK
- Then per-type walk override: the walk override for Spider (BMD type 9) is separate from the walk override list. The global multiplier for Type==3 (note: this is BMD Type, NOT server type) gives Spider 1.2x on all non-DIE actions.

**Correct Spider speeds:** STOP1=0.30, WALK=0.408, ATTACK1/2=0.396, SHOCK=0.60, DIE=0.55

## Player Animation Speeds (ZzzOpenData.cpp:329)

| Action Category | PlaySpeed |
|-----------------|-----------|
| Idle (PLAYER_STOP_MALE) | 0.28 |
| Walk (PLAYER_WALK_MALE) | 0.30 |
| Combat (attacks) | 0.32 |
| Die | 0.45 |
| Shock | 0.40 |

## World Object Animation (Velocity)

Default `o->Velocity = 0.16f` for all world objects (ZzzObject.cpp:4604).
At 25fps: 0.16 * 25 = **4.0 keyframes/sec**.

Animation whitelist (CPU re-skinning enabled): Types 56-57 (MerchantAnimal), 59 (TreasureChest), 60 (Ship), 90 (StreetLight), 95 (Curtain), 96 (Sign), 98 (Carriage), 105 (Waterspout), 110 (Hanging), 118-119 (House04-05), 120 (Tent), 150 (Candle).

## Monster Action Constants (_define.h:483)

```
MONSTER01_STOP1    = 0
MONSTER01_STOP2    = 1
MONSTER01_WALK     = 2
MONSTER01_ATTACK1  = 3
MONSTER01_ATTACK2  = 4
MONSTER01_SHOCK    = 5
MONSTER01_DIE      = 6
MONSTER01_APEAR    = 7
MONSTER01_ATTACK3  = 8
MONSTER01_ATTACK4  = 9
MONSTER01_RUN      = 10
MONSTER01_ATTACK5  = 11
```

## Reference Code

- `ZzzBMD.cpp:Animation()` -- Bone interpolation with slerp
- `ZzzBMD.cpp:PlayAnimation()` -- Frame advance
- `ZzzOpenData.cpp:OpenMonsterModel()` -- PlaySpeed table
- `ZzzCharacter.cpp:CharacterAnimation()` -- Per-frame animation driver
- `ZzzScene.cpp:MainScene()` -- 40ms tick loop
