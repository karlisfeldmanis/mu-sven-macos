extends SceneTree

## Headless Texture Converter
##
## Usage:
##   godot --headless --script scripts/convert_textures.gd -- <input_dir> <output_dir>
##
## Example:
##   godot --headless --script scripts/convert_textures.gd -- raw_data/Player assets/players/textures

const TextureConverter = preload("res://addons/mu_tools/texture_converter_headless.gd")

func _init() -> void:
	var args = OS.get_cmdline_user_args()
	
	if args.size() < 2:
		print_usage()
		quit(1)
		return
	
	var input_dir = args[0]
	var output_dir = args[1]
	
	print("[Texture Converter] Starting conversion...")
	print("  Input: ", input_dir)
	print("  Output: ", output_dir)
	
	if not DirAccess.dir_exists_absolute(input_dir):
		push_error("Input directory does not exist: " + input_dir)
		quit(1)
		return
	
	# Create output directory if it doesn't exist
	if not DirAccess.dir_exists_absolute(output_dir):
		DirAccess.make_dir_recursive_absolute(output_dir)
	
	# Convert all textures
	var converter = TextureConverter.new()
	var result = converter.convert_directory(input_dir, output_dir)
	
	print("\n[Texture Converter] Conversion complete!")
	print("  Processed: ", result.total)
	print("  Success: ", result.success)
	print("  Failed: ", result.failed)
	
	quit(0 if result.failed == 0 else 1)

func print_usage() -> void:
	print("Usage: godot --headless --script scripts/convert_textures.gd -- <input_dir> <output_dir>")
	print("")
	print("Converts MuOnline .ozj/.ozt textures to PNG format")
	print("")
	print("Arguments:")
	print("  input_dir   - Directory containing .ozj/.ozt files")
	print("  output_dir  - Directory to save converted PNG files")
