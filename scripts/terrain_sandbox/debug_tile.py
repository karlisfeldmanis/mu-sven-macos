
import os
import sys

# Add parent dir to path to import parse_terrain
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from parse_terrain import MUTerrainParser

class TileDebugger:
    def __init__(self, data_path, world_id=1):
        self.parser = MUTerrainParser(data_path, world_id)
        self.mapping = self.parser.parse_mapping()
        self.attributes = self.parser.parse_attributes()
        
    def apply_tile_symmetry(self, ux, uy, sym):
        res_x, res_y = ux, uy
        if sym & 1: res_x = 1.0 - res_x
        if sym & 2: res_y = 1.0 - res_y
        if sym & 4: res_x, res_y = 1.0 - res_y, res_x
        return res_x, res_y

    def debug_tile(self, wx, wy):
        print(f"--- Debugging Tile ({wx}, {wy}) ---")
        
        if not (0 <= wx < 256 and 0 <= wy < 256):
            print("Out of bounds")
            return

        idx = wy * 256 + wx
        
        # Mapping
        tex1 = self.mapping["layer1"][idx]
        tex2 = self.mapping["layer2"][idx]
        alpha = self.mapping["alpha"][idx]
        print(f"Texture Index 1: {tex1}")
        print(f"Texture Index 2: {tex2}")
        print(f"Alpha: {alpha}")

        # Attributes / Symmetry
        sym_byte = self.attributes["symmetry"][idx]
        col_byte = self.attributes["collision"][idx]
        print(f"Symmetry Byte: {sym_byte} (0x{sym_byte:02x})")
        print(f"Collision Byte: {col_byte} (0x{col_byte:02x})")
        
        # Interpret Symmetry Bits
        flip_x = (sym_byte & 1) != 0
        flip_y = (sym_byte & 2) != 0
        swap_xy = (sym_byte & 4) != 0
        print(f"  Bit 0 (Flip X): {flip_x}")
        print(f"  Bit 1 (Flip Y): {flip_y}")
        print(f"  Bit 2 (Swap XY / Rot90): {swap_xy}")
        
        # Calculate UVs for corners (Seamless logic from fix_tiling.py)
        # Using scale 1.0 for "Authentic" view check
        scale = 1.0
        print(f"Calculating UVs (Scale {scale} - World Space):")
        
        corners = [(0,0), (1,0), (0,1), (1,1)]
        for cx, cy in corners:
            # Local UVs
            fx, fy = float(cx), float(cy)
            
            # Apply Symmetry
            sfx, sfy = self.apply_tile_symmetry(fx, fy, sym_byte)
            
            # World UVs (No offset)
            uv_x = (float(wx) + sfx) * scale
            uv_y = (float(wy) + sfy) * scale
            
            print(f"  Corner ({cx}, {cy}) -> Local ({sfx:.2f}, {sfy:.2f}) -> World ({uv_x:.2f}, {uv_y:.2f})")

if __name__ == "__main__":
    debugger = TileDebugger("reference/MuMain/src/bin/Data/World1", world_id=1)
    
    # Target some interesting tiles
    debugger.debug_tile(128, 110) # Road entrance
    debugger.debug_tile(128, 128) # City center
    debugger.debug_tile(135, 131) # Symmetry test spot
