import os
import io
import struct
import numpy as np
from PIL import Image

TERRAIN_SIZE = 256
TERRAIN_SCALE = 100.0

MAP_XOR_KEY = [
    0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
    0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
]

def decrypt_map_file(data):
    decrypted = bytearray(len(data))
    map_key = 0x5E
    for i in range(len(data)):
        src_byte = data[i]
        xor_byte = MAP_XOR_KEY[i % 16]
        val = (src_byte ^ xor_byte) - map_key
        decrypted[i] = val & 0xFF
        map_key = (src_byte + 0x3D) & 0xFF
    return decrypted

def load_ozj(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) > 24 and data[24:26] == b'\xff\xd8':
        data = data[24:]
    elif data[0:2] != b'\xff\xd8':
         # Try other OZJ variants if needed, but 24 is standard for Mu
         pass
    return Image.open(io.BytesIO(data)).convert('RGB')

def get_texture_path(idx, world_id):
    base_names = [
        "TileGrass01", "TileGrass02", "TileGround01", "TileGround02", "TileGround03",
        "TileWater01", "TileWood01", "TileRock01", "TileRock02", "TileRock03",
        "TileRock04", "TileRock05", "TileRock06", "TileRock07"
    ]
    if idx < 14:
        name = base_names[idx]
    else:
        name = f"ExtTile{idx-13:02d}"
    
    # Try WorldX folder first, then Data folder
    path = f"reference/MuMain/src/bin/Data/World{world_id}/{name}.OZJ"
    if os.path.exists(path): return path
    path = f"reference/MuMain/src/bin/Data/{name}.OZJ"
    if os.path.exists(path): return path
    return None

def calculate_normals(heightmap):
    normals = np.zeros((TERRAIN_SIZE, TERRAIN_SIZE, 3))
    for y in range(TERRAIN_SIZE):
        for x in range(TERRAIN_SIZE):
            x_prev = max(0, x - 1)
            x_next = min(TERRAIN_SIZE - 1, x + 1)
            y_prev = max(0, y - 1)
            y_next = min(TERRAIN_SIZE - 1, y + 1)
            dzdx = (heightmap[y, x_next] - heightmap[y, x_prev]) / (2.0 * TERRAIN_SCALE)
            dzdy = (heightmap[y_next, x] - heightmap[y_prev, x]) / (2.0 * TERRAIN_SCALE)
            n = np.array([-dzdx, -dzdy, 1.0])
            normals[y, x] = n / np.linalg.norm(n)
    return normals

def run_advanced_simulation():
    world_id = 1
    data_dir = f"reference/MuMain/src/bin/Data/World{world_id}"
    
    # 1. Parse Heightmap
    height_path = os.path.join(data_dir, "TerrainHeight.OZB")
    with open(height_path, "rb") as f:
        h_data = f.read()
    raw_heights = h_data[-65536:]
    heights = np.array([float(b) for b in raw_heights]).reshape((256, 256))
    
    # 2. Parse Mapping
    map_path = os.path.join(data_dir, f"EncTerrain{world_id}.map")
    with open(map_path, "rb") as f:
        map_raw = f.read()
    map_dec = decrypt_map_file(map_raw)
    l1 = np.array(list(map_dec[2:65538])).reshape((256, 256))
    l2 = np.array(list(map_dec[65538:131074])).reshape((256, 256))
    alpha = np.array(list(map_dec[131074:196610])).reshape((256, 256)) / 255.0
    
    # 3. Load Lightmap
    light_path = os.path.join(data_dir, "TerrainLight.OZJ")
    lightmap = load_ozj(light_path).resize((256, 256))
    light_data = np.array(lightmap) / 255.0
    
    # 4. Calculate Normals
    normals = calculate_normals(heights)
    
    # 5. Composite a 64x64 area (center)
    CX, CY = 128, 128
    SIZE = 64
    HALF = SIZE // 2
    
    output = np.zeros((SIZE * 16, SIZE * 16, 3))
    
    # Cache for loaded textures
    tex_cache = {}

    print(f"Rendering {SIZE}x{SIZE} area...")
    for y in range(CY - HALF, CY + HALF):
        for x in range(CX - HALF, CX + HALF):
            idx1 = l1[y, x]
            idx2 = l2[y, x]
            a = alpha[y, x]
            
            # Load textures
            if idx1 not in tex_cache:
                p = get_texture_path(idx1, world_id)
                tex_cache[idx1] = load_ozj(p) if p else None
            if idx2 not in tex_cache:
                p = get_texture_path(idx2, world_id)
                tex_cache[idx2] = load_ozj(p) if p else None
                
            t1 = tex_cache[idx1]
            t2 = tex_cache[idx2]
            
            # Sub-tile rendering (16x16 pixels per tile for preview)
            # In actual MU, it's 64x64 pixels per tile
            # We'll just take a pixel for now or do simplified tiling
            
            # Calculate lighting factor
            light_dir = np.array([0.5, -0.5, 0.5])
            light_dir = light_dir / np.linalg.norm(light_dir)
            luminosity = np.dot(normals[y, x], light_dir) + 0.5
            luminosity = np.clip(luminosity, 0.0, 1.0)
            
            tile_light = light_data[y, x] * luminosity
            
            # Base color
            c1 = np.array([0.5, 0.5, 0.5]) # Default
            if t1:
                # Sample middle of tile texture
                c1 = np.array(t1.getpixel((32, 32))) / 255.0
            
            # Overlay color
            c2 = c1
            if idx2 != 255 and t2:
                c2 = np.array(t2.getpixel((32, 32))) / 255.0
            
            # Blend
            final_c = c1 * (1.0 - a) + c2 * a
            final_c *= tile_light
            
            py = (y - (CY - HALF)) * 16
            px = (x - (CX - HALF)) * 16
            output[py : py + 16, px : px + 16] = final_c
            
    res_img = Image.fromarray((output * 255).astype(np.uint8))
    res_img.save("lorencia_advanced_sim.png")
    print("Saved lorencia_advanced_sim.png")

if __name__ == "__main__":
    run_advanced_simulation()
