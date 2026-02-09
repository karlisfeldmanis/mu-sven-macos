
import os
import struct
import math

class MUTerrainParser:
    TERRAIN_SIZE = 256
    MAP_XOR_KEY = [
        0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
        0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
    ]
    BUX_CODE = [0xFC, 0xCF, 0xAB]

    def __init__(self, data_path, world_id=1):
        self.data_path = data_path
        self.world_id = world_id
        
    def decrypt_map_file(self, data):
        decrypted = bytearray(len(data))
        map_key = 0x5E
        
        for i in range(len(data)):
            src_byte = data[i]
            xor_byte = self.MAP_XOR_KEY[i % 16]
            
            # (pbySrc[i] ^ byMapXorKey[i % 16]) - (BYTE)wMapKey;
            # Python bytes are unsigned 0-255, so we need to handle overflow manually with & 0xFF
            val = (src_byte ^ xor_byte) - map_key
            decrypted[i] = val & 0xFF
            
            # wMapKey = pbySrc[i] + 0x3D;
            map_key = (src_byte + 0x3D) & 0xFF
            
        return decrypted

    def parse_heightmap(self, filename="TerrainHeight.OZB"):
        path = os.path.join(self.data_path, filename)
        if not os.path.exists(path):
            print(f"[ERROR] Heightmap not found: {path}")
            return None
            
        with open(path, "rb") as f:
            data = f.read()
            
        # Last 256*256 bytes are the height data
        expected_size = self.TERRAIN_SIZE * self.TERRAIN_SIZE
        if len(data) < expected_size:
            print(f"[ERROR] Heightmap too small: {len(data)}")
            return None
            
        raw_heights = data[-expected_size:]
        
        # Parse into float array with MU->Godot scaling
        # Formula: (val * 1.5 - 500.0) / 100.0
        heights = []
        for b in raw_heights:
            h = (float(b) * 1.5 - 500.0) / 100.0
            heights.append(h)
            
        print(f"[PARSER] Heightmap parsed. Min: {min(heights):.2f}, Max: {max(heights):.2f}")
        return heights

    def parse_mapping(self, filename=None):
        if not filename:
            filename = f"EncTerrain{self.world_id}.map"
        path = os.path.join(self.data_path, filename)
        
        if not os.path.exists(path):
            print(f"[ERROR] Mapping file not found: {path}")
            return None
            
        with open(path, "rb") as f:
            raw_data = f.read()
            
        data = self.decrypt_map_file(raw_data)
        
        # Valid sizes: Dungeon (196610 - 2 header), Lorencia (196609 - 1 header)
        # We dynamic skip to get the last 3 * 256*256 bytes
        layer_size = self.TERRAIN_SIZE * self.TERRAIN_SIZE
        expected_data_size = layer_size * 3
        
        if len(data) < expected_data_size:
             print(f"[ERROR] Map file too small after decryption")
             return None
             
        start_ptr = len(data) - expected_data_size
        
        layer1 = data[start_ptr : start_ptr + layer_size]
        layer2 = data[start_ptr + layer_size : start_ptr + layer_size * 2]
        alpha = data[start_ptr + layer_size * 2 : start_ptr + layer_size * 3]
        
        print(f"[PARSER] Mapping parsed. Layer1 unique: {len(set(layer1))}, Layer2 unique: {len(set(layer2))}")
        return {
            "layer1": list(layer1),
            "layer2": list(layer2),
            "alpha": [a / 255.0 for a in alpha]
        }

    def parse_attributes(self, filename=None):
        if not filename:
            filename = f"EncTerrain{self.world_id}.att"
        path = os.path.join(self.data_path, filename)
        
        if not os.path.exists(path):
            print(f"[ERROR] Attribute file not found: {path}")
            return None
            
        with open(path, "rb") as f:
            raw_data = f.read()
            
        data = self.decrypt_map_file(raw_data)
        
        # Apply BuxConvert
        data = bytearray(data)
        for i in range(len(data)):
            data[i] ^= self.BUX_CODE[i % 3]
            
        # Parse logic
        # Header + Data. Data is usually WORD (2 bytes) or BYTE depending on format version 0/1.
        # Original code checks if size is 131076 (65536 * 2 + 4) -> WORD
        # Or 65540 -> BYTE
        
        attr_map = []
        if len(data) == 131076:
            # Word format, skip 4 bytes header, read every 2nd byte (low byte)
            ptr = 4
            for _ in range(self.TERRAIN_SIZE * self.TERRAIN_SIZE):
                 attr_map.append(data[ptr]) # Low byte
                 ptr += 2
        elif len(data) >= 65536:
             # Byte format or fallback
             offset = len(data) - 65536
             attr_map = list(data[offset:])
        else:
             print("[ERROR] Unknown attribute file size")
             return None
             
        print(f"[PARSER] Attributes parsed. Size: {len(attr_map)}")
        return attr_map

    def parse_objects(self, filename=None):
        if not filename:
            filename = f"EncTerrain{self.world_id}.obj"
        path = os.path.join(self.data_path, filename)
        
        if not os.path.exists(path):
            return []
            
        with open(path, "rb") as f:
            raw_data = f.read()
            
        data = self.decrypt_map_file(raw_data)
        
        ptr = 2 # Skip header
        count = struct.unpack_from("<h", data, ptr)[0]
        ptr += 2
        
        objects = []
        for i in range(count):
             # struct { short type; float x,y,z; float rx,ry,rz; float scale; }
             # 2 + 12 + 12 + 4 = 30 bytes
             type_id = struct.unpack_from("<h", data, ptr)[0]
             x, y, z = struct.unpack_from("<fff", data, ptr + 2)
             rx, ry, rz = struct.unpack_from("<fff", data, ptr + 14)
             scale = struct.unpack_from("<f", data, ptr + 26)[0]
             
             objects.append({
                 "type": type_id,
                 "pos": (x, y, z),
                 "rot": (rx, ry, rz),
                 "scale": scale
             })
             ptr += 30
             
        print(f"[PARSER] Objects parsed: {len(objects)}")
        return objects

    def load_texture_image(self, filename):
        path = os.path.join(self.data_path, filename)
        exts = [".OZJ", ".ozj", ".OZT", ".ozt", ".tga", ".TGA", ".jpg", ".JPG"]
        
        found_path = None
        current_ext = ""
        for ext in exts:
            test_path = path + ext
            if os.path.exists(test_path):
                found_path = test_path
                current_ext = ext.lower()
                break
                
        if not found_path:
            world_dir = f"World{self.world_id}"
            path_w = os.path.join(self.data_path, world_dir, filename)
            for ext in exts:
                test_path = path_w + ext
                if os.path.exists(test_path):
                    found_path = test_path
                    current_ext = ext.lower()
                    break
        
        if not found_path:
            return None
            
        try:
            from PIL import Image, ImageFile
            import io
            ImageFile.LOAD_TRUNCATED_IMAGES = True
            
            with open(found_path, "rb") as f:
                raw_data = f.read()
            
            # --- SVEN (Main 5.2) PARITY LOGIC ---
            if current_ext in [".ozj", ".jpg"]:
                # OZJ/JPG: Always skip exactly 24 bytes in SVEN
                # even if it starts with FFD8 at 0
                if len(raw_data) > 24:
                    data = raw_data[24:]
                    return Image.open(io.BytesIO(data)).convert("RGB")
                return None
                
            elif current_ext in [".ozt", ".tga"]:
                # OZT/TGA: Skip exactly 4 bytes in SVEN
                # then manual TGA decode (BGRA -> RGBA + Y Flip)
                if len(raw_data) > 4:
                    data = raw_data[4:]
                    # Simple TGA reader parity
                    # Header is 18 bytes
                    width = data[12] | (data[13] << 8)
                    height = data[14] | (data[15] << 8)
                    pixel_depth = data[16]
                    descriptor = data[17]
                    
                    if pixel_depth == 32:
                        pixels = data[18:]
                        # Convert BGRA to RGBA
                        rgba_data = bytearray(len(pixels))
                        for i in range(0, len(pixels), 4):
                             rgba_data[i] = pixels[i+2]   # R
                             rgba_data[i+1] = pixels[i+1] # G
                             rgba_data[i+2] = pixels[i]   # B
                             rgba_data[i+3] = pixels[i+3] # A
                        
                        img = Image.frombytes("RGBA", (width, height), bytes(rgba_data))
                        # SVEN: Flip Y if descriptor bit 5 is 0
                        if not (descriptor & 0x20):
                             img = img.transpose(Image.FLIP_TOP_BOTTOM)
                        return img.convert("RGB")
                return None
                 
            return None
        except Exception as e:
            print(f"[WARN] Failed to load {filename}: {e}")
            return None

    def get_texture_mapping(self):
        # SVEN Mapping Parity
        mapping = {}
        # 0-13: Base
        base_names = [
            "TileGrass01", "TileGrass02", "TileGround01", "TileGround02", "TileGround03",
            "TileWater01", "TileWood01", "TileRock01", "TileRock02", "TileRock03",
            "TileRock04", "TileRock05", "TileRock06", "TileRock07"
        ]
        for i, name in enumerate(base_names): mapping[i] = name
        # 14-255: ExtTileXX
        for i in range(14, 256): mapping[i] = f"ExtTile{i-13:02d}"
        return mapping

    def export_preview(self, output_path="terrain_preview.png"):
        try:
             from PIL import Image, ImageDraw
        except ImportError:
             print("[WARN] PIL not installed, skipping image preview")
             return

        print("[PREVIEW] Loading textures...")
        mapping_names = self.get_texture_mapping()
        texture_cache = {} # idx -> (r,g,b) average color
        
        # Pre-load average colors for all used textures
        map_data = self.parse_mapping()
        if not map_data: return
        
        used_indices = set(map_data["layer1"]) | set(map_data["layer2"])
        
        for idx in used_indices:
            if idx in mapping_names:
                name = mapping_names[idx]
                img = self.load_texture_image(name)
                if img:
                    # Calculate average color
                    img = img.resize((1, 1), Image.Resampling.LANCZOS)
                    texture_cache[idx] = img.getpixel((0, 0))
                else:
                    # Fallback colors
                    if idx == 5: texture_cache[idx] = (0, 0, 200) # Water
                    elif idx <= 1: texture_cache[idx] = (0, 100, 0) # Grass
                    else: texture_cache[idx] = (100, 100, 100) # Rock/Ground
            else:
                texture_cache[idx] = (0, 0, 0)

        print("[PREVIEW] Generating image...")
        img = Image.new("RGB", (self.TERRAIN_SIZE, self.TERRAIN_SIZE))
        pixels = img.load()
        
        layer1 = map_data["layer1"]
        layer2 = map_data["layer2"]
        alpha = map_data["alpha"]
        
        for y in range(self.TERRAIN_SIZE):
            for x in range(self.TERRAIN_SIZE):
                idx = y * self.TERRAIN_SIZE + x
                
                # Flip Y for image (Godot/MU coord parity)
                # MU Y is South->North usually, but our parser reads standard
                # Let's write Standard Top-Down
                
                idx1 = layer1[idx]
                idx2 = layer2[idx]
                a = alpha[idx]
                
                c1 = texture_cache.get(idx1, (0,0,0))
                c2 = texture_cache.get(idx2, (0,0,0))
                
                # Blend
                r = int(c1[0] * (1-a) + c2[0] * a)
                g = int(c1[1] * (1-a) + c2[1] * a)
                b = int(c1[2] * (1-a) + c2[2] * a)
                
                pixels[x, y] = (r, g, b)
                
        img.save(output_path)
        print(f"[SUCCESS] Saved textured preview to {output_path}")

    def export_zoomed_preview(self, center_tile=(128, 128), radius=2, tile_res=256, output_path="terrain_zoomed.png"):
        try:
             from PIL import Image
        except ImportError:
             return

        print(f"[PREVIEW] Generating zoomed preview at {center_tile} (Radius: {radius}, Res: {tile_res})...")
        TILE_RES = tile_res
        area_size = radius * 2
        out_res = area_size * TILE_RES
        
        canvas = Image.new("RGB", (out_res, out_res))
        mapping_names = self.get_texture_mapping()
        map_data = self.parse_mapping()
        if not map_data: return
        
        texture_objs = {} # cache PIL images
        
        start_x = center_tile[0] - radius
        start_y = center_tile[1] - radius
        
        for ty in range(area_size):
            for tx in range(area_size):
                world_x = start_x + tx
                world_y = start_y + ty
                
                if world_x < 0 or world_x >= 256 or world_y < 0 or world_y >= 256:
                    continue
                    
                idx = world_y * 256 + world_x
                idx1 = map_data["layer1"][idx]
                idx2 = map_data["layer2"][idx]
                a = map_data["alpha"][idx]
                
                # Load/Resize L1
                if idx1 not in texture_objs:
                    img = self.load_texture_image(mapping_names.get(idx1, ""))
                    if img and img.size != (TILE_RES, TILE_RES):
                        img = img.resize((TILE_RES, TILE_RES), Image.Resampling.LANCZOS)
                    texture_objs[idx1] = img
                
                # Load/Resize L2
                if idx2 not in texture_objs:
                    img = self.load_texture_image(mapping_names.get(idx2, ""))
                    if img and img.size != (TILE_RES, TILE_RES):
                        img = img.resize((TILE_RES, TILE_RES), Image.Resampling.LANCZOS)
                    texture_objs[idx2] = img
                
                img1 = texture_objs[idx1]
                img2 = texture_objs[idx2]
                
                # Composite
                tile_img = None
                if img1 and img2 and a > 0:
                    mask = Image.new("L", (TILE_RES, TILE_RES), int(a * 255))
                    tile_img = Image.composite(img2, img1, mask)
                elif img1:
                    tile_img = img1
                elif img2:
                    tile_img = img2
                
                if tile_img:
                    canvas.paste(tile_img, (tx * TILE_RES, ty * TILE_RES))
                    
        canvas.save(output_path)
        print(f"[SUCCESS] Saved zoomed preview to {output_path}")

if __name__ == "__main__":
    parser = MUTerrainParser("reference/MuMain/src/bin/Data/World1", world_id=1)
    parser.parse_heightmap()
    parser.parse_mapping()
    parser.parse_attributes()
    parser.parse_objects()
    parser.export_preview("terrain_preview.png")
    # City Center: (128, 128) with radius 32, using 64x64 tiles for a clean full view
    parser.export_zoomed_preview(center_tile=(128, 128), radius=32, tile_res=64, output_path="city_center.png")
