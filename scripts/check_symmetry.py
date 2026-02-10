
import os
import struct

def decrypt_data(data, xor_key):
    decrypted = bytearray(len(data))
    map_key = 0x5E
    for i in range(len(data)):
        src_byte = data[i]
        xor_byte = xor_key[i % 16]
        val = (src_byte ^ xor_byte) - map_key
        decrypted[i] = val & 0xFF
        map_key = (src_byte + 0x3D) & 0xFF
    return decrypted

def bux_convert(data):
    bux_code = [0xFC, 0xCF, 0xAB]
    for i in range(len(data)):
        data[i] ^= bux_code[i % 3]
    return data

def main():
    MAP_XOR_KEY = [
        0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
        0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
    ]
    
    path = "reference/MuMain/src/bin/Data/World1/EncTerrain1.att"
    if not os.path.exists(path):
        print(f"Error: {path} not found")
        return

    with open(path, "rb") as f:
        raw_data = f.read()
    
    decrypted = decrypt_data(raw_data, MAP_XOR_KEY)
    final_data = bux_convert(decrypted)
    
    # Check size
    if len(final_data) == 131076:
        print("WORD format detected (131076 bytes)")
        ptr = 4
        # Area for city floor is around 128, 128
        # We'll dump symmetry for a 10x10 area around 130, 131
        start_x, start_y = 125, 125
        size = 15
        
        print(f"Symmetry Grid ({start_x},{start_y} to {start_x+size},{start_y+size}):")
        for y in range(start_y, start_y + size):
            line = []
            for x in range(start_x, start_x + size):
                idx = y * 256 + x
                sym = final_data[4 + idx * 2 + 1]
                line.append(f"{sym:02x}")
            print(" ".join(line))
    else:
        print(f"BYTE format or unknown size: {len(final_data)}")

if __name__ == "__main__":
    main()
