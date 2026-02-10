#!/usr/bin/env python3
"""
Deep analysis of TileGround03 rendering in SVEN vs Godot.
Goal: Find exact parameters for parity.
"""

import os
import sys
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from parse_terrain import MUTerrainParser
from PIL import Image

class TileGround03Analyzer:
    def __init__(self):
        self.parser = MUTerrainParser("reference/MuMain/src/bin/Data/World1", world_id=1)
        self.mapping = self.parser.parse_mapping()
        self.attrs = self.parser.parse_attributes()

    def find_tileground03_areas(self):
        """Find all TileGround03 tiles in the map"""
        locations = []
        for y in range(256):
            for x in range(256):
                idx = y * 256 + x
                if self.mapping["layer1"][idx] == 4:  # TileGround03
                    sym = self.attrs["symmetry"][idx] if self.attrs else 0
                    locations.append((x, y, sym))
        return locations

    def analyze_symmetry_patterns(self, locations):
        """Analyze what symmetry values are used"""
        sym_counts = {}
        for x, y, sym in locations:
            sym_counts[sym] = sym_counts.get(sym, 0) + 1
        print("\n=== TileGround03 Symmetry Distribution ===")
        for sym, count in sorted(sym_counts.items()):
            print(f"  Symmetry {sym}: {count} tiles")
            print(f"    Bin: {bin(sym)}")
            if sym & 1: print("      - FlipX")
            if sym & 2: print("      - FlipY")
            if sym & 4: print("      - Rot90")
        return sym_counts

    def render_comparison(self, center, radius=4):
        """Render multiple variations to find SVEN match"""
        print(f"\n=== Rendering TileGround03 at {center} ===")

        # Load texture
        tex = self.parser.load_texture_image("TileGround03")
        if not tex:
            print("ERROR: Could not load TileGround03")
            return

        tex = tex.convert("RGB")
        print(f"Texture size: {tex.size}")

        TILE_PX = 128  # High resolution for analysis
        canvas_size = radius * 2 * TILE_PX

        # Test configurations
        configs = [
            ("1_scale025_nosym", 0.25, False, 0),
            ("2_scale10_nosym", 1.0, False, 0),
            ("3_scale10_sym4", 1.0, True, 4),
            ("4_scale10_checker", 1.0, True, None),  # Checkerboard
        ]

        for name, scale, use_sym, sym_override in configs:
            canvas = Image.new("RGB", (canvas_size, canvas_size))

            for ty in range(radius * 2):
                for tx in range(radius * 2):
                    wx = center[0] - radius + tx
                    wy = center[1] - radius + ty

                    if not (0 <= wx < 256 and 0 <= wy < 256):
                        continue

                    midx = wy * 256 + wx
                    if self.mapping["layer1"][midx] != 4:
                        # Not TileGround03, skip
                        tile = Image.new("RGB", (TILE_PX, TILE_PX), (50, 50, 50))
                        canvas.paste(tile, (tx * TILE_PX, ty * TILE_PX))
                        continue

                    # Get symmetry
                    tile_sym = self.attrs["symmetry"][midx] if self.attrs and use_sym else 0
                    if sym_override is not None:
                        tile_sym = sym_override
                    elif name.endswith("checker"):
                        # Checkerboard pattern
                        gx, gy = wx % 2, wy % 2
                        tile_sym = gx | (gy << 1)

                    # Render tile
                    tile_img = Image.new("RGB", (TILE_PX, TILE_PX))
                    pix = tile_img.load()
                    src_pix = tex.load()
                    W, H = tex.size

                    for py in range(TILE_PX):
                        for px in range(TILE_PX):
                            # Normalized coords 0-1
                            fx, fy = px / float(TILE_PX), py / float(TILE_PX)

                            # Apply symmetry
                            sfx, sfy = fx, fy
                            if tile_sym & 1: sfx = 1.0 - fx
                            if tile_sym & 2: sfy = 1.0 - fy
                            if tile_sym & 4: sfx, sfy = 1.0 - sfy, fx

                            # World-space UV
                            uv_x = (float(wx) + sfx) * scale
                            uv_y = (float(wy) + sfy) * scale

                            src_x = int((uv_x * W) % W)
                            src_y = int((uv_y * H) % H)
                            pix[px, py] = src_pix[src_x, src_y]

                    canvas.paste(tile_img, (tx * TILE_PX, ty * TILE_PX))

            output = f"tileground03_{name}.png"
            canvas.save(output)
            print(f"  Saved: {output}")

if __name__ == "__main__":
    analyzer = TileGround03Analyzer()

    # Find all TileGround03 locations
    locations = analyzer.find_tileground03_areas()
    print(f"Found {len(locations)} TileGround03 tiles")

    # Analyze symmetry patterns
    analyzer.analyze_symmetry_patterns(locations)

    # Render comparison at city center (124, 121)
    analyzer.render_comparison((124, 121), radius=4)

    print("\n✓ Analysis complete")
