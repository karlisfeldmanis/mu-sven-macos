import numpy as np
import os

TERRAIN_SIZE = 256

def find_tiles(tile_idx):
    path = "reference/MuMain/src/bin/Data/World1/Terrain.map"
    if not os.path.exists(path):
        path = "reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
    with open(path, 'rb') as f:
        data = bytearray(f.read())
        
    map_xor_key = [0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2]
    map_key = 0x5E
    for i in range(len(data)):
        src = data[i]
        data[i] = ((src ^ map_xor_key[i % 16]) - map_key) & 0xFF
        map_key = (src + 0x3D) & 0xFF
        
    expected_size = TERRAIN_SIZE * TERRAIN_SIZE * 3
    ptr = max(0, len(data) - expected_size)
    layer1 = np.frombuffer(data[ptr:ptr+TERRAIN_SIZE*TERRAIN_SIZE], dtype=np.uint8).reshape((256, 256))
    
    # Find coordinates where layer1 == tile_idx
    coords = np.where(layer1 == tile_idx)
    if len(coords[0]) == 0:
        print(f"No tiles with index {tile_idx} found.")
        return []
        
    # Print a few and the average
    y_avg = np.mean(coords[0])
    x_avg = np.mean(coords[1])
    print(f"Tile {tile_idx} average position: ({x_avg:.2f}, {y_avg:.2f})")
    
    # Range
    y_min, y_max = np.min(coords[0]), np.max(coords[0])
    x_min, x_max = np.min(coords[1]), np.max(coords[1])
    print(f"Tile {tile_idx} bounding box: X[{x_min}-{x_max}] Y[{y_min}-{y_max}]")
    
    return coords

if __name__ == "__main__":
    path = "reference/MuMain/src/bin/Data/World1/Terrain.map"
    if not os.path.exists(path):
        path = "reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
    with open(path, 'rb') as f:
        data = bytearray(f.read())
        
    map_xor_key = [0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2]
    map_key = 0x5E
    for i in range(len(data)):
        src = data[i]
        data[i] = ((src ^ map_xor_key[i % 16]) - map_key) & 0xFF
        map_key = (src + 0x3D) & 0xFF
        
    expected_size = TERRAIN_SIZE * TERRAIN_SIZE * 3
    ptr = max(0, len(data) - expected_size)
    layer1 = np.frombuffer(data[ptr:ptr+TERRAIN_SIZE*TERRAIN_SIZE], dtype=np.uint8).reshape((256, 256))
    
    unique_indices = np.unique(layer1)
    print(f"Unique Layer 1 indices: {unique_indices}")
    
    for idx in unique_indices:
        coords = np.where(layer1 == idx)
        y_avg = np.mean(coords[0])
        x_avg = np.mean(coords[1])
        print(f"Index {idx:3}: count={len(coords[0]):5}, avg=({x_avg:6.2f}, {y_avg:6.2f})")
