# MU Online Remaster - Godot 4 Migration

This project migrates the MuOnline character system from the [Sven-n/MuMain](https://github.com/sven-n/MuMain) C++ source to Godot 4.

## Project Modes

### 🔧 Headless Mode (Asset Conversion)
Convert MuOnline assets without opening the Godot editor.

### 🎮 Standalone Mode (Runtime Application)
Run the game as a standalone application without the editor.

## Quick Start

### 1. Headless Asset Conversion

```bash
# Convert all textures
godot --headless --script scripts/convert_textures.gd -- raw_data/Player assets/players/textures

# Convert a single BMD file
godot --headless --script scripts/convert_bmd.gd -- raw_data/Player/Player.bmd assets/players/meshes

# Batch convert everything
chmod +x scripts/batch_convert.sh
./scripts/batch_convert.sh
```

### 2. Run Standalone Application

```bash
# Run without editor
godot --path . main.tscn

# Or export and run the build
godot --export-release "Linux/X11" builds/linux/mu_remaster.x86_64
./builds/linux/mu_remaster.x86_64
```

## Project Structure

```
mu_remaster/
├── addons/mu_tools/              # Import plugins
│   ├── texture_importer.gd       # Editor texture importer
│   ├── texture_converter_headless.gd  # CLI texture converter
│   ├── bmd_parser.gd             # BMD file parser
│   ├── bmd_converter_headless.gd # CLI BMD converter
│   └── coordinate_utils.gd       # Coordinate conversion
├── scripts/                      # Headless conversion scripts
│   ├── convert_textures.gd       # Texture CLI tool
│   ├── convert_bmd.gd            # BMD CLI tool
│   └── batch_convert.sh          # Batch conversion
├── raw_data/                     # Original MU files (gitignored)
├── assets/                       # Converted Godot assets
├── main.tscn                     # Standalone app entry point
├── main.gd                       # Runtime asset loader
└── export_presets.cfg            # Build configurations
```

## Workflow

### Development Workflow (with Editor)
1. Place `.bmd`/`.ozj`/`.ozt` files in `raw_data/Player/`
2. Open project in Godot 4 editor
3. Files auto-import via EditorImportPlugin
4. Test in `scenes/test_scene.tscn`

### Production Workflow (Headless)
1. Place source files in `raw_data/`
2. Run `./scripts/batch_convert.sh`
3. Converted assets appear in `assets/`
4. Export standalone build
5. Distribute build with converted assets

## Headless Commands

### Environment Setup
```bash
# Set Godot binary path (if not in PATH)
export GODOT_BIN=/path/to/godot

# Or use system Godot
export GODOT_BIN=godot
```

### Conversion Commands
```bash
# Convert textures from a directory
godot --headless --path . --script scripts/convert_textures.gd -- \
    raw_data/Player \
    assets/players/textures

# Convert single BMD file
godot --headless --path . --script scripts/convert_bmd.gd -- \
    raw_data/Player/Player.bmd \
    assets/players/meshes

# Batch convert all assets
./scripts/batch_convert.sh
```

### Export Standalone Build
```bash
# Linux
godot --headless --export-release "Linux/X11" builds/linux/mu_remaster.x86_64

# macOS
godot --headless --export-release "macOS" builds/macos/mu_remaster.zip

# Windows
godot --headless --export-release "Windows Desktop" builds/windows/mu_remaster.exe
```

## Standalone Application

The standalone app (`main.tscn`) loads converted assets at runtime:

**Controls:**
- `R` - Reload character
- `ESC` - Quit

**Features:**
- Runtime asset loading (no editor needed)
- Automatic mesh discovery
- Character rotation demo
- Cross-platform builds

## Migration Phases

### ✅ Phase 1: Texture Extraction
- [x] Editor importer
- [x] Headless converter
- [x] DXT decompression
- [ ] Batch testing

### 🚧 Phase 2: Mesh Reconstruction
- [x] BMD parser
- [x] Mesh builder
- [x] Headless converter
- [ ] UV mapping validation

### 🚧 Phase 3: Skeletal Mapping
- [ ] Skeleton builder
- [ ] Bone hierarchy
- [ ] Headless converter

### 🚧 Phase 4: Animation Bridge
- [ ] Animation parser
- [ ] AnimationLibrary builder
- [ ] Headless converter

## Technical Details

See [Migration Roadmap](/.gemini/antigravity/brain/d8b802df-930d-40a4-aeb3-6ff85f0908b0/migration_roadmap.md) for:
- Binary format specifications
- Coordinate system conversion (Z-up → Y-up)
- Byte-offset calculations
- Testing strategies

## CI/CD Integration

Example GitHub Actions workflow:

```yaml
name: Convert Assets
on: [push]
jobs:
  convert:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Download Godot
        run: wget https://downloads.tuxfamily.org/godotengine/4.3/Godot_v4.3-stable_linux.x86_64.zip
      - name: Extract Godot
        run: unzip Godot_v4.3-stable_linux.x86_64.zip
      - name: Convert Assets
        run: |
          export GODOT_BIN=./Godot_v4.3-stable_linux.x86_64
          ./scripts/batch_convert.sh
      - name: Upload Assets
        uses: actions/upload-artifact@v3
        with:
          name: converted-assets
          path: assets/
```

## License

This project is for educational purposes. MuOnline is a trademark of Webzen Inc.

## References

- [Sven-n/MuMain Repository](https://github.com/sven-n/MuMain)
- [ZzzBMD.cpp](https://github.com/sven-n/MuMain/blob/main/src/ZzzBMD.cpp)
- [ZzzTexture.cpp](https://github.com/sven-n/MuMain/blob/main/src/ZzzTexture.cpp)
