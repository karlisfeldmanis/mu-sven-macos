
# Terrain Fix Sandbox

This directory contains a standalone Python environment for verifying the MU Online terrain rendering logic, specifically focusing on fixing the "staircase" artifacts and tile symmetry.

## Structure
- `venv/`: Separated Python environment with `Pillow` installed.
- `fix_tiling.py`: The core rendering script that simulates different sampling strategies.
- `parse_terrain.py`: (Imported from parent) Data loader for heightmaps and mapping files.

## How to Run
To regenerate the comparison images:
```bash
./venv/bin/python3 fix_tiling.py
```

## Comparisons Generated
1. `1_seamless_025.png`: Default seamless rendering at 0.25 scale.
2. `2_discrete_rock.png`: Explicit 1:1 tiling for rock textures (workaround).
3. `3_row_shift_active.png`: Replication of SVEN's randomized row offsets for Nature textures.
4. `4_seamless_10.png`: High-density seamless tiling (scale 1.0).

These images help visually confirm why isolating Floors from Symmetry is the correct definitive fix.
