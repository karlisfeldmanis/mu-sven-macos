# MU Online to Godot Coordinate System Guide

## Overview

MU Online uses a **Z-up right-handed** coordinate system. Godot uses a **Y-up right-handed** system. The conversion between them is a **cyclic permutation** with determinant +1 (orientation-preserving).

```
MU Space (Z-up):           Godot Space (Y-up):
  X = East                   X = North  (MU Y)
  Y = North                  Y = Up     (MU Z)
  Z = Up                     Z = East   (MU X)
```

## The Golden Rule

```
Godot X = MU Y
Godot Y = MU Z
Godot Z = MU X
```

This applies to **positions, vertices, normals, and rotations** (via similarity transform).

## Key Transforms

### Position (World)
File: `mu_transform_pipeline.gd` — `world_mu_to_godot()`
```gdscript
Vector3(v.y * s, v.z * s, v.x * s)
```

### Position (Local/Model)
File: `mu_transform_pipeline.gd` — `local_mu_to_godot()`
```gdscript
Vector3(v.y, v.z, v.x) * 0.01
```

### Rotation
File: `mu_transform_pipeline.gd` — `mu_rotation_to_quaternion()`

Uses a **pure similarity transform**: `M_godot = W * M_mu * W^T`

Where `W` is the permutation matrix mapping MU axes to Godot axes. The formula is:
```
G[i][j] = M_mu[inv(i)][inv(j)]
```
Where `inv` maps: Godot X(0) -> MU Y(1), Godot Y(1) -> MU Z(2), Godot Z(2) -> MU X(0).

### Scale
File: `mu_coordinate_utils.gd` — `convert_scale()`
```gdscript
Vector3(mu_scale, mu_scale, mu_scale)  # Uniform, NO mirror
```

## Critical Rules (Do NOT Break)

### 1. NO axis mirror in scale
```gdscript
# CORRECT:
return Vector3(mu_scale, mu_scale, mu_scale)

# WRONG — causes swastika fence patterns and reversed object orientations:
return Vector3(mu_scale, mu_scale, -mu_scale)
```
**Why:** Face winding is already corrected in `mesh_builder.gd` by reversing triangle
index order (`[0, 2, 1]`). A Z-mirror in scale is redundant and creates a reflection
that breaks all rotations.

### 2. NO sign flips in rotation similarity transform
```gdscript
# CORRECT (pure similarity transform):
var g02 = m10   # [1][0]
var g12 = m20   # [2][0]
var g20 = m01   # [0][1]
var g21 = m02   # [0][2]

# WRONG — these "compensate" for a Z-mirror that should not exist:
var g02 = -m10
var g12 = -m20
var g20 = -m01
var g21 = -m02
```

### 3. Grass billboard positions must match terrain mesh builder
The terrain mesh builder maps tile coordinates as:
```
Godot X = mu_y (row)
Godot Z = mu_x (column)
```
Grass billboard placement in `mu_terrain.gd` MUST use the same mapping:
```gdscript
# CORRECT:
var pos_vec = Vector3(float(y) + sub_y, h_base, float(x) + sub_x)

# WRONG — transposes grass relative to terrain:
var pos_vec = Vector3(float(x) + sub_x, h_base, float(y) + sub_y)
```

### 4. Heightmap indexing is row-major
```
index = mu_y * 256 + mu_x
```
When converting from Godot world position back to tile index:
```
mu_x = world_pos.z   (inverse of Godot Z = MU X)
mu_y = world_pos.x   (inverse of Godot X = MU Y)
```

## Face Winding

MU Online (DirectX) uses **clockwise** face winding. Godot uses **counter-clockwise**.

This is handled in `mesh_builder.gd` by reversing the triangle index order:
```gdscript
for tri in bmd_mesh.triangles:
    for i in [0, 2, 1]:  # Reversed from [0, 1, 2]
```

Do NOT add a scale mirror to "fix" winding — it is already fixed here.

## Debugging Checklist

If objects appear wrong after changes, check in this order:

1. **Fences form swastikas/broken shapes** → Check `convert_scale()` for axis mirror
2. **Objects face wrong direction** → Check `mu_rotation_to_quaternion()` for sign flips
3. **Grass floats above terrain** → Check grass position mapping matches terrain mesh builder
4. **Objects float above ground** → Check height snapping logic uses correct tile index mapping
