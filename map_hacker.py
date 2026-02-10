import os

MAP_XOR_KEY = [
    0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
    0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
]

def decrypt_map(data):
    dst = bytearray(len(data))
    w_map_key = 0x5E
    for i in range(len(data)):
        xor_val = data[i] ^ MAP_XOR_KEY[i % 16]
        dst[i] = (xor_val - w_map_key) & 0xFF
        w_map_key = (data[i] + 0x3D) & 0xFF
    return dst

def write_pgm(filename, data, width, height):
    with open(filename, 'wb') as f:
        f.write(f"P5\n{width} {height}\n255\n".encode())
        f.write(data)

def analyze_seamlessness(layer1):
    print("\n--- Seamlessness (UV Anchoring) Analysis ---")
    
    # Hypothesis 1: World-space UV anchoring (0.25 scale)
    # If this is true, then if world_pos.x % 4 == 0, we are at the start of a texture repeat.
    # We can check if quads with SAME index across 4x4 blocks have continuous indices.
    
    scale = 0.25
    matches = 0
    checks = 0
    
    for y in range(0, 252, 4):
        for x in range(0, 252, 4):
            # Check a 4x4 block
            indices = []
            for dy in range(4):
                for dx in range(4):
                    indices.append(layer1[(y+dy)*256 + (x+dx)])
            
            # If all 16 quads have the same index, they form a "Seamless Mega-Tile"
            if len(set(indices)) == 1:
                matches += 1
            checks += 1
            
    print(f"4x4 Mega-Tile alignment (World-Space parity): {matches}/{checks} ({matches*100.0/checks:.1f}%)")

def analyze_lorencia(file_path):
    print(f"Analyzing {file_path}...")
    with open(file_path, "rb") as f:
        encrypted_data = f.read()
    
    decrypted_data = decrypt_map(encrypted_data)
    
    ptr = 2
    size = 256 * 256
    layer1 = decrypted_data[ptr:ptr+size]
    ptr += size
    layer2 = decrypted_data[ptr:ptr+size]
    ptr += size
    alpha = decrypted_data[ptr:ptr+size]
    
    # Save as PGM images
    write_pgm("layer1_indices.pgm", layer1, 256, 256)
    
    print("\n--- Pattern Analysis (Layer 1) ---")
    for y in range(8):
        row = [layer1[y*256 + x] for x in range(16)]
        print(f"Row {y:2}: {' '.join(f'{v:3}' for v in row)}")

    analyze_seamlessness(layer1)

if __name__ == "__main__":
    target = "/Users/karlisfeldmanis/Desktop/mu_remaster/reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
    if os.path.exists(target):
        analyze_lorencia(target)
    else:
        print(f"File not found: {target}")
