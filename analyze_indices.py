import struct
import os

def analyze_map(path):
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return
    
    with open(path, 'rb') as f:
        # Skip header if exists (usually 2 bytes version)
        f.read(2)
        data = f.read()
        
    size = 256 * 256
    # layer1, layer2, alpha
    l1 = list(data[0:size])
    
    counts = {}
    for v in l1:
        counts[v] = counts.get(v, 0) + 1
        
    print("Tile Index Usage (Sorted by Frequency):")
    for k in sorted(counts, key=counts.get, reverse=True)[:20]:
        print(f"Index {k:3d}: {counts[k]:5d} tiles")

analyze_map("reference/MuMain/src/bin/Data/World1/EncTerrain1.map")
