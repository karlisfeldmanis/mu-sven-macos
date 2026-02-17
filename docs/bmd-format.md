# BMD Binary Model Format

## Versions & Encryption
- **Versions**: Supports **0xC** (encrypted) and **0xA** (unencrypted).
- **Encryption (version 0xC)**:
  - XOR key (16 bytes, cycling): `[0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2]`
  - Cumulative key: initial `wKey = 0x5E`, per byte: `wKey = (src[i] + 0x3D)`
  - Formula: `dst[i] = (src[i] ^ xorKey[i % 16]) - wKey`

## File Layout
```
"BMD" + version(1)
if version == 0xC: encryptedSize(4) + encryptedPayload(...)

Decrypted payload:
  Name(32)
  NumMeshes(short) NumBones(short) NumActions(short)

  Per mesh:
    NumVertices(short) NumNormals(short) NumTexCoords(short) NumTriangles(short) TextureID(short)
    Vertices:  count * 16 bytes  [Node(2) + Pad(2) + Position(12)]
    Normals:   count * 20 bytes  [Node(2) + Pad(2) + Normal(12) + BindVertex(2) + Pad(2)]
    TexCoords: count * 8 bytes   [U(float) + V(float)]
    Triangles: count * 64 bytes  [see Triangle_t below]
    TextureName(32)

  Per action:
    NumAnimationKeys(short) LockPositions(bool)
    if locked: Positions(count * 12)

  Per bone:
    Dummy(bool)
    if not dummy: Name(32) Parent(short) then per-action: Position(keys*12) Rotation(keys*12)
```

## Triangle_t On-Disk Layout (64 bytes per triangle)
The file stores Triangle_t2 format (MSVC struct with padding). We read the first 34 meaningful bytes:
```
offset 0:  char Polygon        (1 byte, value 3=tri or 4=quad)
offset 1:  [padding]           (1 byte)
offset 2:  short VertexIndex[4]    (8 bytes)
offset 10: short NormalIndex[4]    (8 bytes)
offset 18: short TexCoordIndex[4]  (8 bytes)
offset 26: [LightMapCoord data]   (we read into EdgeTriangleIndex, unused)
offset 34: [more padding/lightmap] (we skip)
```
**Important**: We `memcpy` 34 bytes then advance ptr by 64. Failure to use the 64-byte stride leads to alignment shifts and `std::length_error` during parsing.

## Byte Sizes

| Element | Size | Notes |
|---------|------|-------|
| Vertex | 16 bytes | Node(2) + Pad(2) + Position(12) |
| Normal | 20 bytes | Node(2) + Pad(2) + Normal(12) + BindVertex(2) + Pad(2) |
| TexCoord | 8 bytes | U(float) + V(float) |
| Triangle stride | 64 bytes | On-disk with padding |
| Triangle read | 34 bytes | Actual data we use |
