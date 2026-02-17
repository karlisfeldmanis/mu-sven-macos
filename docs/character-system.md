# Character System

Reference: ZzzCharacter.cpp, ZzzOpenData.cpp, _enum.h

## DK Naked Character (HeroCharacter)

5-part body (Helm, Armor, Pants, Gloves, Boots) skinned against `Player.bmd` skeleton (60 bones, 0 meshes, 284 actions).

### Player.bmd Action Indices (from _enum.h)

Enum values map **1:1 to BMD action indices**. No offset needed. `MAX_PLAYER_ACTION = 284`.

| Range | Actions |
|-------|---------|
| 1-14 | Idle (Stop Male/Female/weapon variants, Fly, Ride) |
| 15-24 | Walk (Male/Female/weapon variants, Swim) |
| 25-37 | Run (weapon variants, Fly, Ride) |
| 38-59 | Combat (Fist=38, Sword R1=39, R2=40, Bow, Ride attacks) |
| 60-66 | DK Sword Skills (SKILL_SWORD1-5, Wheel, Fury Strike) |
| 67 | Heal (SKILL_VITALITY) |
| 70-71 | Spear/DeathStab |
| 146-154 | Magic Skills (Energy Ball, Teleport, Flash, Inferno, Hell Fire) |
| 186-221 | Emotes (Defense, Greeting, Gesture, Cry, Respect, etc.) |
| 230 | Shock (hit stun) |
| 231-232 | Die |
| 233-240 | Sit/Healing/Pose |

**Lesson learned**: `YDG_ADD_SKILL_RIDING_ANIMATIONS` ifdef is never defined -- those 6 extra entries do NOT exist in the Player.bmd.

### Player Animation Speeds (ZzzOpenData.cpp:329)

| Action | PlaySpeed | Keys/sec |
|--------|-----------|----------|
| Idle (STOP_MALE) | 0.28 | 7.0 |
| Walk (WALK_MALE) | 0.30 | 7.5 |
| Combat (attacks) | 0.32 | 8.0 |
| Shock | 0.40 | 10.0 |
| Die | 0.45 | 11.25 |

### Weapon-Specific Idle/Walk Actions

| Category | Idle Action | Walk Action | Attach Bone |
|----------|------------|------------|-------------|
| Sword | 4 (STOP_SWORD) | 17 (WALK_SWORD) | 33 (R Hand) |
| Axe | 4 | 17 | 33 |
| Mace | 4 | 17 | 33 |
| Spear | 6 (STOP_SPEAR) | 19 (WALK_SPEAR) | 33 |
| Bow | 8 (STOP_BOW) | 20 (WALK_BOW) | 42 (L Hand) |
| Staff | 10 (STOP_WAND) | 22 (WALK_WAND) | 42 |
| Shield | 4 | 17 | 42 |

## DK Stats (MuEmu-0.97k DefaultClassInfo.txt)

| Stat | Starting Value |
|------|---------------|
| STR | 28 |
| DEX | 20 |
| VIT | 25 |
| ENE | 10 |
| Base HP | 110 |
| HP per Level | 2.0 |
| HP per VIT | 3.0 |
| Points per Level | 5 |

### Derived Combat Formulas

```
DamageMin = STR / 8 + weaponDamageMin
DamageMax = STR / 4 + weaponDamageMax
Defense = DEX / 3 + equipmentDefense
AttackSuccessRate = Level * 5 + DEX * 3/2 + STR / 4
DefenseSuccessRate = DEX / 3
MaxHP = BaseHP + Level * LevelLife + (VIT - BaseVIT) * VITtoLife
```

### XP Table (gObjSetExperienceTable)

`XP_for_level = (Level + 9) * Level * Level * 10`

Cubic curve, MaxLevel=400.

## Movement

- Click-to-move: `MoveTo(target)` sets destination
- `ProcessMovement(dt)` interpolates position with terrain height tracking
- Player MoveSpeed: base 10, run 12-15, wings/fenrir 15-19 (units per tick)
- At 25fps: 250-375 world units/sec

## Reference Code

- `ZzzCharacter.cpp:CreateCharacterPointer()` -- Weapon rendering config
- `ZzzOpenData.cpp:329-451` -- Player PlaySpeed table
- `_enum.h` -- Action index enum (PLAYER_SET=0)
- `DefaultClassInfo.txt` -- Class starting stats
