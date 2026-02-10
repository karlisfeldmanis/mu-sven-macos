import numpy as np
from PIL import Image
import os

# MU Parity Constants
TERRAIN_SIZE = 256
TILE_SIZE = 64 # Texture coverage in MU units (matches SVEN 64.f)

def verify_tiling(map_path, output_name):
    print(f"Analyzing {map_path}...")
    
    # MU Mapping Decryption Logic
    with open(map_path, 'rb') as f:
        data = bytearray(f.read())
        
    # Standard MU Decryption (Simplified for verification)
    map_xor_key = [0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2]
    map_key = 0x5E
    for i in range(len(data)):
        src = data[i]
        data[i] = ((src ^ map_xor_key[i % 16]) - map_key) & 0xFF
        map_key = (src + 0x3D) & 0xFF
        
    # Skip header
    expected_size = TERRAIN_SIZE * TERRAIN_SIZE * 3
    ptr = max(0, len(data) - expected_size)
    layer1 = np.frombuffer(data[ptr:ptr+TERRAIN_SIZE*TERRAIN_SIZE], dtype=np.uint8).reshape((256, 256))
    
    # Focus on a 16x16 area in the town (Lorencia center)
    # Approx (128, 128)
    cx, cy = 120, 130
    h = 32
    zoom = layer1[cy:cy+h, cx:cx+h]
    
    # Create the comparison image
    # We will simulate how a 256x256 texture wraps across this 32x32 cell area
    # Current (Incorrect): scale = 1.0 (1 texture repetition per cell)
    # Parity (Correct): scale = 64/256 = 0.25 (1 texture repetition per 4 cells)
    
    res = Image.new('RGB', (1024, 512), (30, 30, 30))
    
    def simulate_render(cell_scale, label):
        canvas = np.zeros((256, 256, 3), dtype=np.uint8)
        # Mock texture (Checkerboard 2x2 for visibility of repeats)
        tex = np.zeros((256, 256, 3), dtype=np.uint8)
        tex[:128, :128] = [200, 200, 200]
        tex[128:, 128:] = [200, 200, 200]
        tex[:128, 128:] = [100, 100, 100]
        tex[128:, :128] = [100, 100, 100]
        
        # Grid Size: 32 cells (zoom)
        # Each cell is 8x8 pixels in the 256x256 view
        cell_px = 8
        for y in range(h):
            for x in range(h):
                # Calculate UVs for this cell
                # sv = (yf * 64/w)
                u = (cx + x) * cell_scale
                v = (cy + y) * cell_scale
                
                # Fetch color from texture at this UV
                tx = int((u * 256) % 256)
                ty = int((v * 256) % 256)
                color = tex[ty, tx]
                
                # Draw cell
                canvas[y*cell_px:(y+1)*cell_px, x*cell_px:(x+1)*cell_px] = color
                
                # Add Grid Border
                canvas[y*cell_px, x*cell_px:(x+1)*cell_px] = [50, 50, 50]
                canvas[y*cell_px:(y+1)*cell_px, x*cell_px] = [50, 50, 50]
                
        return Image.fromarray(canvas)

    img_incorrect = simulate_render(1.0, "Current (1:1)")
    img_parity = simulate_render(0.25, "Parity (4:1)")
    
    res.paste(img_incorrect, (0, 0))
    res.paste(img_parity, (512, 0))
    
    res.save(output_name)
    print(f"Saved comparison to {output_name}")

if __name__ == "__main__":
    path = "reference/MuMain/src/bin/Data/World1/Terrain1.map"
    verify_tiling(path, "tiling_comparison.png")
