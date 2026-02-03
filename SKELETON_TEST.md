# Skeleton Visualization Tool

## Quick Start

Open the skeleton test scene in Godot:

```bash
godot --editor /Users/karlisfeldmanis/Desktop/mu_remaster/scenes/skeleton_test.tscn
```

## What This Shows

The visualizer displays:
- **Green spheres** = Bone positions
- **Cyan lines** = Parent-child bone connections
- **RGB axes** = Bone orientations (Red=X, Green=Y, Blue=Z)
- **White labels** = Bone names

## Verifying Humanoid Structure

A correct humanoid skeleton should show:
1. **Root bone** at pelvis/hip area
2. **Spine chain** going upward (spine → chest → neck → head)
3. **Arm chains** branching from shoulders (shoulder → upper arm → forearm → hand)
4. **Leg chains** from pelvis (thigh → calf → foot)

## Console Output

The script prints:
- Total bone count
- Bone hierarchy with parent relationships
- Local and global positions for each bone
- List of root bones

## Files Created

- [`skeleton_visualizer.gd`](file:///Users/karlisfeldmanis/Desktop/mu_remaster/addons/mu_tools/skeleton_visualizer.gd) - Visualization tool
- [`skeleton_test.gd`](file:///Users/karlisfeldmanis/Desktop/mu_remaster/scenes/skeleton_test.gd) - Test scene script
- [`skeleton_test.tscn`](file:///Users/karlisfeldmanis/Desktop/mu_remaster/scenes/skeleton_test.tscn) - Godot scene
