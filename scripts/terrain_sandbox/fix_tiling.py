
import os
import sys
from PIL import Image

# Add parent dir to path to import parse_terrain
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from parse_terrain import MUTerrainParser

class TerrainFixSandbox:
    def __init__(self, data_path, world_id=1):
        self.parser = MUTerrainParser(data_path, world_id)
        self.mapping = self.parser.parse_mapping()
        self.textures = {} # idx -> Image
        
    def load_used_textures(self, indices):
        mapping_names = self.parser.get_texture_mapping()
        for idx in set(indices):
            if idx not in self.textures:
                name = mapping_names.get(idx, "")
                img = self.parser.load_texture_image(name)
                if img:
                    # MU textures are usually 256x256
                    self.textures[idx] = img.convert("RGB")
                else:
                    # Fallback colors
                    fallback = Image.new("RGB", (256, 256), (128, 128, 128))
                    self.textures[idx] = fallback

    def apply_tile_symmetry(self, ux, uy, sym):
        res_x, res_y = ux, uy
        if sym & 1: res_x = 1.0 - res_x
        if sym & 2: res_y = 1.0 - res_y
        if sym & 4: res_x, res_y = 1.0 - res_y, res_x
        return res_x, res_y

    def render_area(self, center=(128, 137), radius=8, output_name="sandbox.png", 
                    scale=0.25, discrete_indices=None, enable_row_shift=False,
                    enable_col_shift=False, use_symmetry=True, tile_px=64,
                    sym_override=None, checker_mode=None, offset_mode=None,
                    mirrored_repeat=None):
        """
        Renders a specific area with configurable logic.
        """
        if discrete_indices is None:
            discrete_indices = set()
            
        TILE_PX = tile_px
        area_size = radius * 2
        canvas = Image.new("RGB", (area_size * TILE_PX, area_size * TILE_PX))
        
        start_x = center[0] - radius
        start_y = center[1] - radius
        
        # Determine unique indices used
        used_indices = []
        for ty in range(area_size):
            for tx in range(area_size):
                wx, wy = start_x + tx, start_y + ty
                if 0 <= wx < 256 and 0 <= wy < 256:
                    idx = wy * 256 + wx
                    used_indices.append(self.mapping["layer1"][idx])
                    used_indices.append(self.mapping["layer2"][idx])
        self.load_used_textures(used_indices)
        
        # Load symmetry data
        att_res = self.parser.parse_attributes()
        symmetry = att_res.get("symmetry", [0] * 65536) if att_res else ([0] * 65536)
        
        import random
        random.seed(42)
        row_offsets = [random.randint(0, 3) * 0.25 for _ in range(256)]
        col_offsets = [random.randint(0, 3) * 0.25 for _ in range(256)]
        
        mapping_names = self.parser.get_texture_mapping()

        for ty in range(area_size):
            wy = start_y + ty
            row_o = row_offsets[wy] if enable_row_shift else 0.0
            
            for tx in range(area_size):
                wx = start_x + tx
                if not (0 <= wx < 256 and 0 <= wy < 256): continue
                
                midx = wy * 256 + wx
                idx1 = self.mapping["layer1"][midx]
                idx2 = self.mapping["layer2"][midx]
                alpha = self.mapping["alpha"][midx]
                tile_sym = symmetry[midx] if use_symmetry else 0
                if sym_override is not None:
                    tile_sym = sym_override
                if checker_mode:
                    gx, gy = wx % 2, wy % 2
                    if checker_mode == "x": tile_sym = gx
                    elif checker_mode == "y": tile_sym = gy << 1
                    elif checker_mode == "xy": tile_sym = gx | (gy << 1)
                    
                col_o = col_offsets[wx] if enable_col_shift else 0.0
                
                # Sample Tile
                def sample_texture(tex_idx, world_x, world_y, r_off, c_off, sym):
                    img = self.textures.get(tex_idx)
                    if not img: return None
                    
                    name = mapping_names.get(tex_idx, "").lower()
                    is_floor = "rock" in name or "stone" in name or "ground" in name or "exttile" in name or "grass" in name
                    
                    # If sym_override is active, we force it even for floors
                    current_sym = 0 if (is_floor and use_symmetry and sym_override is None) else sym

                    if tex_idx in discrete_indices:
                        part = img.resize((256, 256), Image.Resampling.LANCZOS)
                        if current_sym & 1: part = part.transpose(Image.FLIP_LEFT_RIGHT)
                        if current_sym & 2: part = part.transpose(Image.FLIP_TOP_BOTTOM)
                        if current_sym & 4: part = part.transpose(Image.ROTATE_90)
                        return part.resize((TILE_PX, TILE_PX))
                    else:
                        tile_img = Image.new("RGB", (TILE_PX, TILE_PX))
                        pix = tile_img.load()
                        src_pix = img.load()
                        W, H = img.size
                        
                        for py in range(TILE_PX):
                            for px in range(TILE_PX):
                                fx, fy = px / float(TILE_PX), py / float(TILE_PX)
                                sfx, sfy = self.apply_tile_symmetry(fx, fy, current_sym)
                                
                                # World UVs (Seamless)
                                uv_x = (float(world_x + r_off) + sfx) * scale
                                uv_y = (float(world_y + c_off) + sfy) * scale
                                
                                src_x = int((uv_x * W) % W)
                                src_y = int((uv_y * H) % H)
                                pix[px, py] = src_pix[src_x, src_y]
                        return tile_img

                current_r_off = row_o + (0.5 if offset_mode == "x" and wy % 2 else 0)
                current_c_off = col_o + (0.5 if offset_mode == "y" and wx % 2 else 0)
                
                # Apply Mirrored Repeat if requested
                if mirrored_repeat:
                    def mir(v): return abs((v * 0.25) % 2.0 - 1.0)
                    if mirrored_repeat == 'x': current_c_off = mir(wx); current_r_off = wy * 0.25
                    if mirrored_repeat == 'y': current_r_off = mir(wy); current_c_off = wx * 0.25
                    if mirrored_repeat == 'xy': current_c_off = mir(wx); current_r_off = mir(wy)
                
                # Apply Discrete Checkerboard Mirroring (1:1 per tile)
                effective_tile_sym = tile_sym
                if checker_mode == "xy" and scale == 1.0:
                    # Bit 0 = FlipX if wx is odd, Bit 1 = FlipY if wy is odd
                    checker_bits = (wx % 2) | ((wy % 2) << 1)
                    effective_tile_sym ^= checker_bits

                img1 = sample_texture(idx1, wx, wy, current_r_off, current_c_off, effective_tile_sym)
                img2 = sample_texture(idx2, wx, wy, current_r_off, current_c_off, effective_tile_sym)
                
                final_tile = None
                if img1 and img2 and alpha > 0:
                    mask = Image.new("L", (TILE_PX, TILE_PX), int(alpha * 255))
                    final_tile = Image.composite(img2, img1, mask)
                elif img1:
                    final_tile = img1
                elif img2:
                    final_tile = img2
                
                if final_tile:
                    canvas.paste(final_tile, (tx * TILE_PX, ty * TILE_PX))
        
        canvas.save(output_name)
        print(f"Saved {output_name}")

if __name__ == "__main__":
    sandbox = TerrainFixSandbox("reference/MuMain/src/bin/Data/World1", world_id=1)
    
    # EXPORT RAW TEXTURES FOR COMPARISON
    mapping_names = sandbox.parser.get_texture_mapping()
    for idx in [2, 3, 4, 7, 8, 14, 15]: # Ground, Rock, and some Road ExtTiles
        name = mapping_names.get(idx, "")
        img = sandbox.parser.load_texture_image(name)
        if img:
            img.save(f"scripts/terrain_sandbox/raw_idx_{idx}_{name}.png")
            print(f"Exported raw texture: {name} (Index {idx})")

    # 0. THE CITY PICTURE (Lorencia Social Hub)
    # Render the city center (128, 128) with a 50-tile radius for a 100x100 overview.
    # At 32px per tile, this will be a 3200x3200px image.
    # We'll use Scale 1.0 for EVERYTHING in this test to check "Matching"
    sandbox.render_area(center=(128, 128), radius=50, output_name="scripts/terrain_sandbox/lorencia_city_matching_10.png",
                        scale=1.0, tile_px=32, use_symmetry=True)
    
    # Tavern is around (123, 137)
    TAVERN_COORDS = (123, 137)
    
    # 1. Attempt Seamless (What we have now)
    sandbox.render_area(TAVERN_COORDS, radius=4, output_name="scripts/terrain_sandbox/1_seamless_025.png", 
                        scale=0.25, discrete_indices=set([0])) # Grass is seamless
                        
    # 2. Attempt Discrete Rock (What I tried to fix it)
    sandbox.render_area(TAVERN_COORDS, radius=4, output_name="scripts/terrain_sandbox/2_discrete_rock.png", 
                        scale=0.25, discrete_indices=set([7, 8, 9, 10, 11, 12, 13]))
                        
    # 3. Attempt Seamless with Row Shift (Nature Parity)
    sandbox.render_area(TAVERN_COORDS, radius=4, output_name="scripts/terrain_sandbox/3_row_shift_active.png", 
                        scale=0.25, enable_row_shift=True)
                        
    # 4. Attempt Scale 1.0 Seamless (No jumping, just high repeat)
    sandbox.render_area(TAVERN_COORDS, radius=4, output_name="scripts/terrain_sandbox/4_seamless_10.png", 
                        scale=1.0)

    # 5. REPLICATE BROKEN STAIRCASE (Rock Floor)
    sandbox.render_area(TAVERN_COORDS, radius=4, output_name="scripts/terrain_sandbox/5_broken_staircase.png", 
                        scale=0.25, use_symmetry=True)

    # 6. ROAD PARITY SIMULATION
    # Targeting the main road entrance to Lorencia (approx 128, 90 or 128, 110)
    # This area has the detailed "Rock" and "Road" junctions.
    
    # 6a. Authentic MU (Fixed) - Seamless Floor + Symmetric Discrete Roads
    # This is our target for "Perfect Alignment"
    ROAD_ENTRANCE = (128, 110)
    sandbox.render_area(ROAD_ENTRANCE, radius=8, output_name="scripts/terrain_sandbox/6a_authentic_road_flow.png", 
                        scale=0.25, discrete_indices=set(range(14, 256)), use_symmetry=False) # Ignore symmetry for floors

    # 6b. BROKEN SIMULATION (Replicating User's "Staircase" on Roads)
    # Logic: Apply symmetry to the rock/road textures at 0.25 scale.
    sandbox.render_area(ROAD_ENTRANCE, radius=8, output_name="scripts/terrain_sandbox/6b_road_staircase_broken.png", 
                        scale=0.25, use_symmetry=True)
    
    # 6c. Target specific Rock area (190, 10) for pure rock staircase parity
    ROCK_ROAD = (195, 12)
    sandbox.render_area(ROCK_ROAD, radius=4, output_name="scripts/terrain_sandbox/6c_rock_staircase_detail.png", 
                        scale=0.25, use_symmetry=True)
    
    # 13. GROUND SYMMETRY OPTIONS (Connectivity Test)
    SYMMETRY_TEST_SPOT = (135, 131)
    
    # Mode 0: None (Original Seams)
    sandbox.render_area(SYMMETRY_TEST_SPOT, radius=4, output_name="scripts/terrain_sandbox/13a_sym_none.png",
                        scale=1.0, use_symmetry=True, tile_px=64, sym_override=0)
    
    # Mode 1: Flip X (Horizontal Neighbors)
    sandbox.render_area(SYMMETRY_TEST_SPOT, radius=4, output_name="scripts/terrain_sandbox/13b_sym_flip_x.png",
                        scale=1.0, use_symmetry=True, tile_px=64, checker_mode="x")
    
    # Mode 2: Flip Y (Vertical Neighbors - Best for Horizontal Slices?)
    sandbox.render_area(SYMMETRY_TEST_SPOT, radius=4, output_name="scripts/terrain_sandbox/13c_sym_flip_y.png",
                        scale=1.0, use_symmetry=True, tile_px=64, checker_mode="y")
    
    # Mode 3: Flip XY (Full Checkerboard - The "Something Off" look)
    sandbox.render_area(SYMMETRY_TEST_SPOT, radius=4, output_name="scripts/terrain_sandbox/13d_sym_flip_xy.png",
                        scale=1.0, use_symmetry=True, tile_px=64, checker_mode="xy")
    
    # 17. DISCRETE CHECKERBOARD TEST (Standard MU Parity?)
    # Scale 1.0 (Discrete) + Checkerboard XY + Sym 4 (Rot90)
    sandbox.render_area(SYMMETRY_TEST_SPOT, radius=4, output_name="scripts/terrain_sandbox/17_discrete_checker_rot.png",
                        scale=1.0, use_symmetry=True, tile_px=64, checker_mode="xy", sym_override=4)
    
    # Scale 1.0 (Discrete) + Checkerboard XY + Sym 0 (Normal)
    sandbox.render_area(SYMMETRY_TEST_SPOT, radius=4, output_name="scripts/terrain_sandbox/17_discrete_checker_normal.png",
                        scale=1.0, use_symmetry=True, tile_px=64, checker_mode="xy", sym_override=0)
    NATURE_SPOT = (145, 128)
    sandbox.render_area(NATURE_SPOT, radius=8, output_name="scripts/terrain_sandbox/11c_nature_seamless.png",
                        scale=1.0, use_symmetry=False, tile_px=128)
    
    # 10. IDENTICAL COMPARISON: SOURCE vs OUR SIDE
    # Exact same area, exact same zoom.
    LORENCIA_ZOOM = (135, 131) # A good urban spot with Ground02
    
    # 10a. SOURCE (SVEN Authentic Logic)
    # Using 1.0 Scale + Sym 4 (The "Legit" Vertical Slice)
    sandbox.render_area(LORENCIA_ZOOM, radius=4, output_name="scripts/terrain_sandbox/10a_source_sven_legit.png",
                        scale=1.0, use_symmetry=True, tile_px=128, sym_override=4)

    # 10b. OUR SIDE (Current Implementation)
    # Using our Hybrid categorization + XOR Symmetry
    # (Since Ground02 is Cat 0 / Sym 4, this should be IDENTICAL to 10a)
    sandbox.render_area(LORENCIA_ZOOM, radius=4, output_name="scripts/terrain_sandbox/10b_our_side_parity.png",
                        scale=1.0, use_symmetry=True, tile_px=128, sym_override=4)
                        
    # 10c. THE DIFFERENCE (Nature Area)
    # To show why Hybrid is better for the rest of the map
    ROCK_VIEW = (200, 100) # Deep nature
    sandbox.render_area(ROCK_VIEW, radius=4, output_name="scripts/terrain_sandbox/10c_nature_hybrid_test.png",
                        scale=0.25, use_symmetry=False, tile_px=128)
