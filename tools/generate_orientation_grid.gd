@tool
extends SceneTree

const MUTerrainParser = preload("res://addons/mu_tools/mu_terrain_parser.gd")

func _init():
	var parser = MUTerrainParser.new()
	var path = "res://reference/MuMain/src/bin/Data/World1/EncTerrain1.map"
	var map_data = parser.parse_mapping_file(path)
	
	if not map_data:
		print("Failed to load map data")
		quit()
		return

	# Mapping colors
	var colors = {
		0: Color.DARK_GREEN,
		1: Color.GREEN,
		2: Color.LIME_GREEN,
		3: Color.SADDLE_BROWN,
		4: Color.SANDY_BROWN,
		5: Color.BLUE,
		6: Color.GRAY,
		7: Color.DARK_GRAY,
		8: Color.BLACK
	}
	
	var grid_width = 256 * 4
	var grid_height = 256 * 2
	var final_img = Image.create(grid_width, grid_height, false, Image.FORMAT_RGB8)
	
	for mode in range(8):
		var base_x = (mode % 4) * 256
		var base_y = (mode / 4) * 256
		
		for y in range(256):
			for x in range(256):
				var data_x = x
				var data_y = y
				
				# Get data index based on mode
				var idx = 0
				match mode:
					0: idx = y * 256 + x        # Identity
					1: idx = y * 256 + (255-x)  # Flip X
					2: idx = (255-y) * 256 + x  # Flip Y
					3: idx = (255-y) * 256 + (255-x) # Rotate 180
					4: idx = x * 256 + y        # Transpose (Swap)
					5: idx = x * 256 + (255-y)  # Rotate 90 CW + Flip
					6: idx = (255-x) * 256 + y  # Rotate 90 CCW + Flip
					7: idx = (255-x) * 256 + (255-y) # Rotate 180 + Swap
				
				var val = map_data.layer1[idx]
				var color = colors.get(val, Color.PURPLE)
				
				# Add labels for identification
				if x < 40 and y < 10:
					color = Color.WHITE
					
				final_img.set_pixel(base_x + x, base_y + y, color)
				
	final_img.save_png("debug_orientation_grid.png")
	print("[Diagnostic] Saved debug_orientation_grid.png")
	quit()
