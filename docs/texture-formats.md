# Texture Formats

## OZJ Format (JPEG variant)

- Optional 24-byte MU header before JPEG data
- Detect JPEG magic `0xFF 0xD8` at offset 0 or 24
- Decompress with TurboJPEG to RGB (no BOTTOMUP flag - keep top-to-bottom)
- MU uses DirectX UV convention (V=0 = top of image); native JPEG order matches this in OpenGL

## OZT Format (TGA variant)

- Optional 4 or 24-byte MU header before TGA header
- Detect TGA by checking `imageType` (byte 2) for value 2 or 10 at offset+2
- **Do NOT use TGA idLength** (byte 0) -- MU OZT files don't follow standard TGA ID convention. Pixel data always starts at `offset + 18`.
- Supports type 2 (uncompressed) and type 10 (RLE compressed)
- Supports 24-bit (BGR) and 32-bit (BGRA, has alpha for transparency)
- **Always V-flip** rows: MU stores data bottom-to-top (standard TGA), flip to top-to-bottom so v=0 maps to top of image (matching DirectX UV convention in OpenGL)

## Texture Resolution Priority
When BMD references `texture.tga` but actual files are `.OZT`/`.OZJ`:
- If original extension is `.tga`/`.ozt`/`.bmp` -> try TGA variants first (preserves alpha)
- If original extension is `.jpg`/`.ozj` -> try JPEG variants first
- This prevents loading JPEG (no alpha) when TGA (with alpha) is needed for transparency

## Texture Script Flags
Parsed from texture filenames: `basename_FLAGS.ext` where FLAGS = combination of R, H, N, S.
- `_R` -> bright (additive blend: `GL_ONE, GL_ONE` + depth write off)
- `_H` -> hidden (skip rendering entirely)
- `_N` -> noneBlend (disable blending, render opaque)
- Only valid if ALL characters after last `_` are recognized flags
