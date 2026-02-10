#!/usr/bin/env python3
"""
Generate full Lorencia city overview using SVEN C++ rendering logic.
This implements the exact same algorithm as the original MU Online client.
"""

import os
import sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

# Import the terrain sandbox which contains SVEN parity logic
from terrain_sandbox.fix_tiling import TerrainFixSandbox

def generate_lorencia_images():
    """Generate various Lorencia city views"""
    print("=" * 60)
    print("Lorencia City Generator - SVEN C++ Logic")
    print("=" * 60)

    # Initialize with World1 data
    sandbox = TerrainFixSandbox("reference/MuMain/src/bin/Data/World1", world_id=1)

    # 1. Full Map Overview (256x256 tiles at 16px each = 4096x4096)
    print("\n[1/3] Generating full map (256x256 tiles)...")
    print("  Resolution: 4096x4096 pixels")
    print("  This may take 1-2 minutes...")
    sandbox.render_area(
        center=(128, 128),
        radius=128,
        output_name="lorencia_full_map.png",
        scale=1.0,
        tile_px=16,
        use_symmetry=True
    )
    print("  ✓ Saved: lorencia_full_map.png")

    # 2. City Center High Detail (100x100 tiles at 64px each = 6400x6400)
    print("\n[2/3] Generating city center high detail...")
    print("  Resolution: 6400x6400 pixels")
    print("  Center: (128, 128)")
    sandbox.render_area(
        center=(128, 128),
        radius=50,
        output_name="lorencia_city_center_hd.png",
        scale=1.0,
        tile_px=64,
        use_symmetry=True
    )
    print("  ✓ Saved: lorencia_city_center_hd.png")

    # 3. City Overview (100x100 tiles at 32px = 3200x3200)
    print("\n[3/3] Generating city overview...")
    print("  Resolution: 3200x3200 pixels")
    sandbox.render_area(
        center=(128, 128),
        radius=50,
        output_name="lorencia_city_overview.png",
        scale=1.0,
        tile_px=32,
        use_symmetry=True
    )
    print("  ✓ Saved: lorencia_city_overview.png")

    print("\n" + "=" * 60)
    print("✓ All Lorencia images generated successfully!")
    print("=" * 60)

    # Print key technical details
    print("\nTechnical Details:")
    print("  • Algorithm: SVEN C++ parity")
    print("  • Scale: 1.0 (discrete 1:1 tile mapping)")
    print("  • Symmetry: ATT file data (per-tile)")
    print("  • Layer blending: Alpha map compositing")
    print("  • Texture source: World1/ directory")

if __name__ == "__main__":
    generate_lorencia_images()
