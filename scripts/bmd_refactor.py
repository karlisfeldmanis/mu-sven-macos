import struct
import os

def map_file_decrypt(enc_data):
    """
    Parity with SVEN ZzzLodTerrain.h: MapFileDecrypt
    """
    xor_key = [
        0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
        0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2
    ]

    dec_data = bytearray()
    w_map_key = 0x5E

    for i in range(len(enc_data)):
        val = (enc_data[i] ^ xor_key[i % 16]) - w_map_key
        dec_data.append(val & 0xFF)

        # update key for next byte
        w_map_key = (enc_data[i] + 0x3D) & 0xFF

    return dec_data

class BMDParser:
    def __init__(self, file_path):
        self.file_path = file_path
        self.version = 0
        self.model_name = ""
        self.num_meshes = 0
        self.num_bones = 0
        self.num_actions = 0
        self.meshes = []

    def parse(self):
        print(f"[*] Parsing {self.file_path}...")
        with open(self.file_path, "rb") as f:
            data = f.read()

        if data[:3] != b"BMD":
            print("[!] Invalid header")
            return False

        ptr = 3
        self.version = data[ptr]
        ptr += 1

        print(f"[*] Version: {hex(self.version)}")

        dec_data = None
        if self.version == 0x0C:
            enc_size = struct.unpack("<I", data[ptr:ptr+4])[0]
            ptr += 4
            print(f"[*] Encrypted data size: {enc_size}")
            dec_data = map_file_decrypt(data[ptr:ptr+enc_size])
            ptr = 0
        elif self.version == 0x0A:
            # Version 0xA is not encrypted
            ptr = 4
            dec_data = data
        else:
            print(f"[!] Unsupported version: {self.version}")
            return False

        # Name (32 bytes)
        name_bytes = dec_data[ptr:ptr+32]
        self.model_name = name_bytes.split(b"\0")[0].decode('ascii', errors='ignore')
        print(f"[*] Model Name: {self.model_name}")
        ptr += 32

        # Counts
        self.num_meshes, self.num_bones, self.num_actions = struct.unpack("<HHH", dec_data[ptr:ptr+6])
        ptr += 6
        print(f"[*] Meshes: {self.num_meshes}, Bones: {self.num_bones}, Actions: {self.num_actions}")

        for m_idx in range(self.num_meshes):
            mesh = self.parse_mesh(dec_data, ptr)
            self.meshes.append(mesh)
            ptr = mesh['end_ptr']

        return True

    def parse_mesh(self, data, ptr):
        # Mesh header: 5 x u16 = 10 bytes
        n_v, n_n, n_t, n_tri, tex_idx = struct.unpack("<HHHHH", data[ptr:ptr+10])
        ptr += 10
        print(f"    - Mesh: V={n_v}, N={n_n}, UV={n_t}, Tri={n_tri}, Tex={tex_idx}")

        # Struct sizes are version-independent (MSVC alignment):
        #   Vertex_t    = 16 bytes (short Node + 2pad + vec3 Position)
        #   Normal_t    = 20 bytes (short Node + 2pad + vec3 Normal + short BindVertex + 2pad)
        #   TexCoord_t  =  8 bytes (float U + float V)
        #   Triangle_t2 = 64 bytes (see layout below)

        # Vertices — 16 bytes each
        vertices = []
        vertex_nodes = []
        for _ in range(n_v):
            v_data = struct.unpack("<hhfff", data[ptr:ptr+16])
            vertex_nodes.append(v_data[0])  # Node (bone index)
            # v_data[1] = padding
            vertices.append(v_data[2:5])     # (x, y, z)
            ptr += 16

        # Normals — 20 bytes each
        normals = []
        normal_nodes = []
        normal_bind_verts = []
        for _ in range(n_n):
            n_data = struct.unpack("<hhfffhh", data[ptr:ptr+20])
            normal_nodes.append(n_data[0])       # Node
            # n_data[1] = padding
            normals.append(n_data[2:5])           # (nx, ny, nz)
            normal_bind_verts.append(n_data[5])   # BindVertex
            # n_data[6] = padding
            ptr += 20

        # UVs — 8 bytes each
        uvs = []
        for _ in range(n_t):
            uv_data = struct.unpack("<ff", data[ptr:ptr+8])
            uvs.append(uv_data)
            ptr += 8

        # Triangles — 64 bytes each (Triangle_t2 layout):
        #   Offset  0: char  Polygon          (1B)
        #   Offset  1: pad                    (1B)
        #   Offset  2: short VertexIndex[4]   (8B)
        #   Offset 10: short NormalIndex[4]   (8B)
        #   Offset 18: short TexCoordIndex[4] (8B)
        #   Offset 26: pad                    (2B)
        #   Offset 28: TexCoord_t LightMapCoord[4] (32B)
        #   Offset 60: short LightMapIndexes  (2B)
        #   Offset 62: pad                    (2B)
        triangles = []
        for _ in range(n_tri):
            tri_start = ptr
            poly_type = struct.unpack("<b", data[ptr:ptr+1])[0]
            # Read indices at offset 2 (skip 1 byte padding)
            indices = struct.unpack("<4h4h4h", data[ptr+2:ptr+26])
            v_idxs = indices[0:4]
            n_idxs = indices[4:8]
            uv_idxs = indices[8:12]

            tri = {
                'poly': poly_type,
                'v': v_idxs[:3],
                'n': n_idxs[:3],
                'uv': uv_idxs[:3],
            }
            triangles.append(tri)

            # Quad: emit second triangle (0, 2, 3)
            if poly_type == 4:
                tri2 = {
                    'poly': poly_type,
                    'v': (v_idxs[0], v_idxs[2], v_idxs[3]),
                    'n': (n_idxs[0], n_idxs[2], n_idxs[3]),
                    'uv': (uv_idxs[0], uv_idxs[2], uv_idxs[3]),
                }
                triangles.append(tri2)

            ptr = tri_start + 64  # Always advance by sizeof(Triangle_t2)

        # Texture filename (32 bytes)
        tex_bytes = data[ptr:ptr+32]
        tex_name = tex_bytes.split(b"\0")[0].decode('ascii', errors='ignore')
        print(f"      - Texture: {tex_name}")
        ptr += 32

        return {
            'vertices': vertices,
            'vertex_nodes': vertex_nodes,
            'normals': normals,
            'normal_nodes': normal_nodes,
            'normal_bind_verts': normal_bind_verts,
            'uvs': uvs,
            'triangles': triangles,
            'tex_name': tex_name,
            'n_v': n_v,
            'n_n': n_n,
            'n_t': n_t,
            'end_ptr': ptr
        }

    def export_obj(self, output_path):
        print(f"[*] Exporting to {output_path}...")
        with open(output_path, "w") as f:
            f.write("# MU Online BMD -> OBJ (bmd_refactor.py)\n")

            v_offset = 1
            vt_offset = 1
            vn_offset = 1

            for m_idx, mesh in enumerate(self.meshes):
                # Skip effect meshes (no vertices)
                if mesh['n_v'] == 0:
                    continue

                f.write(f"g mesh_{m_idx}\n")

                for v in mesh['vertices']:
                    f.write(f"v {v[0]} {v[1]} {v[2]}\n")

                for uv in mesh['uvs']:
                    f.write(f"vt {uv[0]} {1.0 - uv[1]}\n")

                for n in mesh['normals']:
                    f.write(f"vn {n[0]} {n[1]} {n[2]}\n")

                for tri in mesh['triangles']:
                    vi = [idx + v_offset for idx in tri['v']]
                    ti = [idx + vt_offset for idx in tri['uv']]
                    ni = [idx + vn_offset for idx in tri['n']]
                    f.write(f"f {vi[0]}/{ti[0]}/{ni[0]}"
                            f" {vi[1]}/{ti[1]}/{ni[1]}"
                            f" {vi[2]}/{ti[2]}/{ni[2]}\n")

                v_offset += mesh['n_v']
                vt_offset += mesh['n_t']
                vn_offset += mesh['n_n']

if __name__ == "__main__":
    import sys
    target = "reference/MuMain/src/bin/Data/Player/ArmorClass01.bmd"
    if len(sys.argv) > 1:
        target = sys.argv[1]

    parser = BMDParser(target)
    if parser.parse():
        out_name = os.path.basename(target).replace(".bmd", ".obj")
        parser.export_obj(out_name)
        print(f"Success: {out_name}")
    else:
        print("Failed to parse")
