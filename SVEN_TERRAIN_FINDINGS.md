# SVEN Terrain Rendering - Reverse Engineering Findings

## Summary
Complete analysis of MU Online (SVEN client) terrain rendering through Python simulation and comparison with Godot implementation.

## Key Findings

### 1. Texture Mapping System
- **Base textures (0-13)**: Standard terrain tiles
  - 0-1: Grass
  - 2-4: Ground (stone paths)
  - 5: Water
  - 6: Wood
  - 7-13: Rock
- **Extended textures (14-255)**: ExtTile01-ExtTile242 for roads/details

### 2. Scale System
**Purpose**: Controls texture tiling frequency

- **Seamless (0.25 scale)**: For nature textures (grass, trees)
  - Formula: `UV = world_pos * (64.0 / texture_width)`
  - Creates continuous repeating patterns

- **Discrete (1.0 scale)**: For floors/roads (TileGround, ExtTile)
  - Formula: `UV = world_pos * 1.0`
  - Each world tile gets exactly one texture copy

### 3. Symmetry System
**Purpose**: Breaks up visual repetition through flipping/rotation

- **Bit flags**:
  - Bit 0 (1): Flip X (horizontal)
  - Bit 1 (2): Flip Y (vertical)
  - Bit 4 (4): Rotate 90° CW

- **Application**:
  ```python
  if sym & 1: uv.x = 1.0 - uv.x
  if sym & 2: uv.y = 1.0 - uv.y
  if sym & 4: uv.x, uv.y = 1.0 - uv.y, uv.x
  ```

### 4. Category System
**Purpose**: Determines rendering behavior per texture type

| Category | Type | Scale | Symmetry | Examples |
|----------|------|-------|----------|----------|
| 0 | Discrete Detail (Roads) | 1.0 | Use ATT data | ExtTile01-242 |
| 1 | Seamless Nature | 0.25 | Ignore | TileGrass, TileWood |
| 2 | Water | 1.0 | Ignore | TileWater01 |
| 3 | Seamless Floor | 0.25 | Ignore | TileRock01-07 |
| 4 | Discrete Ground | 1.0 | Force=0 | TileGround01-03 |

### 5. TileGround03 Specific Findings
- **All 194 tiles have Symmetry = 0** (no transformation)
- **Scale = 1.0** (discrete 1:1 mapping)
- **Category = 4** (Discrete Ground)
- **Texture**: 128x128 diagonal stone pattern
- **Rendering**: Uniform tiles, no rotation/flipping

### 6. Lighting System (Original Issue)
**Problem**: Over-bright rendering washing out textures

**Original values**:
```glsl
float luminosity = clamp(diff * 0.2 + 1.5, 1.2, 2.0);  // Too bright!
vec3 light_v = max(lightmap_color * 1.5, vec3(0.6)) * luminosity;
ALBEDO = pow(ALBEDO, vec3(0.8));  // Brightening gamma
```

**Fixed values**:
```glsl
float luminosity = clamp(diff * 0.15 + 1.0, 0.8, 1.3);
vec3 light_v = max(lightmap_color * 1.0, vec3(0.4)) * luminosity;
ALBEDO = pow(ALBEDO, vec3(1.0));  // Neutral gamma
```

## Implementation Status

### ✅ Correctly Implemented
- [x] Texture index mapping (0-255)
- [x] OZJ/OZT decryption and loading
- [x] Scale system (1.0 vs 0.25)
- [x] Category system
- [x] Symmetry bit flags
- [x] TileGround03 discrete rendering
- [x] Lighting balance

### 📋 Future Enhancements
- [ ] Row shift for grass (random UV offsets)
- [ ] Water animation flow
- [ ] Advanced symmetry patterns for roads
- [ ] Mirrored repeat modes

## Testing Methodology

### Python Simulation
Created `scripts/analyze_tileground03.py` to:
1. Find all TileGround03 locations
2. Analyze symmetry distribution
3. Render comparison images with different settings
4. Validate against SVEN reference images

### Results
- Confirmed Scale=1.0, Symmetry=0 for TileGround03
- Identified lighting over-exposure as main visual issue
- Verified texture files are correct (World1/TileGround03.OZJ)

## References
- `addons/mu_tools/parsers/mu_terrain_parser.gd`: Core SVEN data parser
- `addons/mu_tools/rendering/material_helper.gd`: Comprehensive rendering simulation
- `addons/mu_tools/rendering/bmd_mesh_builder.gd`: Godot mesh implementation
- `addons/mu_tools/world/heightmap_node.gd`: Godot terrain loader

## Conclusion
The Godot implementation now matches SVEN's terrain rendering with correct texture mapping, scaling, and lighting. The "rock texture problem" was solved by fixing the over-bright lighting that was washing out the diagonal stone patterns in TileGround03.
