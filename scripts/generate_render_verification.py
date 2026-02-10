
import os
import sys
import math
from PIL import Image

# Add parent dir to path to import parse_terrain
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from parse_terrain import MUTerrainParser


def TERRAIN_INDEX(x, y):
    """SVEN: return y * TERRAIN_SIZE + x; with clamp to [0,255]."""
    x = max(0, min(255, x))
    y = max(0, min(255, y))
    return y * 256 + x


class TerrainVerifier:
    """
    Exact emulation of SVEN's ZzzLodTerrain.cpp rendering pipeline.

    Key C++ functions replicated:
      - FaceTexture()       (line 1447) — UV computation
      - RenderTerrainFace() (line 1514) — layer selection + alpha logic
      - RenderTerrainTile() (line 1662) — quad corner indexing
      - VertexAlpha0-3      (line 1229) — per-vertex alpha

    Quad corner layout (GL_TRIANGLE_FAN order):
      Vertex0 = (xi,   yi  )   TerrainIndex1
      Vertex1 = (xi+1, yi  )   TerrainIndex2
      Vertex2 = (xi+1, yi+1)   TerrainIndex3
      Vertex3 = (xi,   yi+1)   TerrainIndex4

    UV formula (non-water, non-scale):
      Width  = 64.0 / tex_width
      Height = 64.0 / tex_height
      suf = xf * Width;  svf = yf * Height;
      corners: (suf, svf) -> (suf+Width, svf+Height)

    Layer selection (RenderTerrainFace):
      if all 4 corner alphas >= 1.0:
          base = Layer2[TerrainIndex1]       (no alpha pass)
      else:
          base = Layer1[TerrainIndex1]
          alpha-blend Layer2 on top using per-vertex alpha

    All textures: GL_NEAREST sampling, GL_REPEAT wrapping.
    """

    WATER_INDEX = 5  # BITMAP_MAPTILE + 5

    def __init__(self, data_path, world_id=1):
        self.parser = MUTerrainParser(data_path, world_id)
        self.mapping = self.parser.parse_mapping()
        self.attributes = self.parser.parse_attributes()

        self.textures = {}       # idx -> PIL Image (RGBA)
        self.texture_name_map = {}  # idx -> lowercase name
        self.tex_scales = {}     # idx -> (Width, Height) = (64/w, 64/h)

    def load_all_textures(self):
        print("[VERIFIER] Loading all textures...")
        mapping_names = self.parser.get_texture_mapping()
        used_indices = set(self.mapping["layer1"]) | set(self.mapping["layer2"])

        loaded_count = 0
        for idx in used_indices:
            name = mapping_names.get(idx, "")
            if not name:
                continue

            img = self.parser.load_texture_image(name)
            if img:
                self.textures[idx] = img.convert("RGBA")
                self.texture_name_map[idx] = name.lower()
                w, h = img.size
                # C++: Width = 64.f / b->Width; Height = 64.f / b->Height;
                self.tex_scales[idx] = (64.0 / w, 64.0 / h)
                loaded_count += 1
            else:
                self.textures[idx] = Image.new("RGBA", (256, 256), (255, 0, 255, 255))
                self.texture_name_map[idx] = "missing"
                self.tex_scales[idx] = (0.25, 0.25)  # 64/256

        print(f"[VERIFIER] Loaded {loaded_count} textures.")

    def is_water(self, idx):
        """Check if texture index is water (BITMAP_MAPTILE + 5)."""
        if idx == self.WATER_INDEX:
            return True
        name = self.texture_name_map.get(idx, "")
        return "water" in name

    def sample_texture(self, idx, u, v, tile_x, tile_y):
        """
        Sample a terrain texture at sub-tile position (u, v) within tile (tile_x, tile_y).
        Emulates C++ FaceTexture() UV computation exactly.

        C++ (non-water):
            suf = xf * Width;  svf = yf * Height;
            At sub-tile (u,v): tex_u = (xf + u) * Width, tex_v = (yf + v) * Height

        C++ (water):
            suf = xf * Width + WaterMove;
            + TerrainGrassWind wave on V corners
        """
        if idx not in self.textures:
            return (0, 0, 0, 0)

        tex = self.textures[idx]
        w, h = tex.size
        scale_u, scale_v = self.tex_scales.get(idx, (0.25, 0.25))

        if self.is_water(idx):
            # C++: WaterMove = (float)((int)(WorldTime)%20000)*0.00005f;
            # Use a fixed time for static verification
            world_time = 123450  # milliseconds
            water_move = (world_time % 20000) * 0.00005

            # C++: suf += WaterMove (on U axis)
            # C++: WindSpeed = (WorldTime % 720000) * 0.002
            # C++: TerrainGrassWind = sin(WindSpeed + xf*5) * 10
            # C++: svf += TerrainGrassWind * 0.002 (on V axis, per-vertex)
            wind_speed = (world_time % 720000) * 0.002
            grass_wind = math.sin(wind_speed + (tile_x + u) * 5.0) * 10.0
            wave = grass_wind * 0.002

            eff_u = (tile_x + u) * scale_u + water_move
            eff_v = (tile_y + v) * scale_v + wave
        else:
            # C++: tex_u = (xf + u) * Width, tex_v = (yf + v) * Height
            eff_u = (tile_x + u) * scale_u
            eff_v = (tile_y + v) * scale_v

        # GL_REPEAT wrapping
        eff_u = eff_u % 1.0
        eff_v = eff_v % 1.0

        # GL_NEAREST sampling
        px = int(eff_u * w) % w
        py = int(eff_v * h) % h
        return tex.getpixel((px, py))

    def _render_region(self, start_x, start_y, size_tiles, tile_res, output_file):
        """
        Core rendering loop matching SVEN's RenderTerrainTile + RenderTerrainFace.

        For each tile (xi, yi):
          1. Compute 4 corner indices: TerrainIndex1-4
          2. Get 4 corner alphas: a1-a4
          3. Layer selection:
             - If all a1..a4 >= 1.0 → base = Layer2 (no alpha pass)
             - Else → base = Layer1, alpha-blend Layer2 with per-vertex alpha
          4. For each pixel in the tile, bilinearly interpolate alpha
             from the 4 corner values, sample both textures, blend.
        """
        layer1 = self.mapping["layer1"]
        layer2 = self.mapping["layer2"]
        alpha = self.mapping["alpha"]

        img = Image.new("RGB", (size_tiles * tile_res, size_tiles * tile_res))
        pixels = img.load()

        for ty_local in range(size_tiles):
            yi = start_y + ty_local
            if not (0 <= yi < 256):
                continue
            if ty_local % 16 == 0:
                print(f"  Row {ty_local}/{size_tiles}...")

            for tx_local in range(size_tiles):
                xi = start_x + tx_local
                if not (0 <= xi < 256):
                    continue

                # --- SVEN: RenderTerrainTile() quad corner indices ---
                # TerrainIndex1 = TERRAIN_INDEX(xi,   yi  )
                # TerrainIndex2 = TERRAIN_INDEX(xi+1, yi  )
                # TerrainIndex3 = TERRAIN_INDEX(xi+1, yi+1)
                # TerrainIndex4 = TERRAIN_INDEX(xi,   yi+1)
                ti1 = TERRAIN_INDEX(xi,     yi)
                ti2 = TERRAIN_INDEX(xi + 1, yi)
                ti3 = TERRAIN_INDEX(xi + 1, yi + 1)
                ti4 = TERRAIN_INDEX(xi,     yi + 1)

                # 4 corner alphas
                a1 = alpha[ti1]
                a2 = alpha[ti2]
                a3 = alpha[ti3]
                a4 = alpha[ti4]

                # --- SVEN: RenderTerrainFace() layer selection ---
                # Texture index always comes from TerrainIndex1 (top-left corner)
                if a1 >= 1.0 and a2 >= 1.0 and a3 >= 1.0 and a4 >= 1.0:
                    # All corners fully opaque → use Layer2 as sole layer
                    base_idx = layer2[ti1]
                    do_alpha_pass = False
                else:
                    # Base is Layer1; alpha-blend Layer2 on top
                    base_idx = layer1[ti1]
                    do_alpha_pass = True

                # Check if any corner has alpha > 0 for the alpha pass
                any_alpha = a1 > 0.0 or a2 > 0.0 or a3 > 0.0 or a4 > 0.0
                overlay_idx = layer2[ti1] if (do_alpha_pass and any_alpha) else None

                # Skip overlay if index is 255 (no layer2)
                if overlay_idx == 255:
                    overlay_idx = None

                # Determine water state for base texture
                base_water = self.is_water(base_idx)

                for py in range(tile_res):
                    for px in range(tile_res):
                        # Sub-tile coordinates [0, 1)
                        u = (px + 0.5) / tile_res
                        v = (py + 0.5) / tile_res

                        # Sample base texture
                        c1 = self.sample_texture(base_idx, u, v, xi, yi)
                        r, g, b = c1[0], c1[1], c1[2]

                        # --- SVEN: VertexAlpha0-3 per-vertex alpha interpolation ---
                        if overlay_idx is not None:
                            # Bilinear interpolation of the 4 corner alphas
                            # Vertex0=(xi,yi)=a1,  Vertex1=(xi+1,yi)=a2
                            # Vertex3=(xi,yi+1)=a4, Vertex2=(xi+1,yi+1)=a3
                            a_top = a1 * (1.0 - u) + a2 * u
                            a_bot = a4 * (1.0 - u) + a3 * u
                            alpha_interp = a_top * (1.0 - v) + a_bot * v

                            if alpha_interp > 0.001:
                                c2 = self.sample_texture(overlay_idx, u, v, xi, yi)
                                r = int(r * (1.0 - alpha_interp) + c2[0] * alpha_interp)
                                g = int(g * (1.0 - alpha_interp) + c2[1] * alpha_interp)
                                b = int(b * (1.0 - alpha_interp) + c2[2] * alpha_interp)

                        pixels[tx_local * tile_res + px, ty_local * tile_res + py] = (
                            min(255, max(0, r)),
                            min(255, max(0, g)),
                            min(255, max(0, b))
                        )

        img.save(output_file)
        print(f"[VERIFIER] Saved to {output_file}")

    def render_map(self, output_file="verification_render_full.png"):
        """Render the full 256x256 terrain map at 16px per tile."""
        print("[VERIFIER] Rendering 256x256 map...")
        self._render_region(0, 0, 256, 16, output_file)

    def render_zoom(self, center_x, center_y, radius, output_file="verification_render_zoom.png"):
        """Render a zoomed region at 64px per tile."""
        print(f"[VERIFIER] Rendering zoom ({center_x}, {center_y}) r={radius}...")
        size = radius * 2
        self._render_region(center_x - radius, center_y - radius, size, 64, output_file)


if __name__ == "__main__":
    DATA_PATH = os.path.join(os.path.dirname(__file__), "../reference/MuMain/src/bin/Data/World1")

    verifier = TerrainVerifier(DATA_PATH, world_id=1)
    verifier.load_all_textures()

    # Lorencia City Center
    verifier.render_zoom(133, 120, 24, "verification_render_zoom_city.png")
    # Path transition detail
    verifier.render_zoom(148, 116, 8, "verification_render_zoom_path.png")
