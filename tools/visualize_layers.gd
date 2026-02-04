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

	var img = Image.create(256, 256, false, Image.FORMAT_RGB8)
	
	# Mapping colors to indices for visualization
	var colors = {
		0: Color.DARK_GREEN,
		1: Color.GREEN,
		2: Color.LIME_GREEN,
		3: Color.SADDLE_BROWN,
		4: Color.SANDY_BROWN,
		5: Color.BLUE,         # Let's keep 5 as blue for now to see where it is
		6: Color.GRAY,
		7: Color.DARK_GRAY,
		8: Color.BLACK
	}
	
	for y in range(256):
		for x in range(256):
			var idx = y * 256 + x
			var val = map_data.layer1[idx]
			var color = colors.get(val, Color.PURPLE)
			img.set_pixel(x, y, color)
			
	img.save_png("debug_layer1_map.png")
	print("[Diagnostic] Saved debug_layer1_map.png")
	
	# Also do Layer 2
	var img2 = Image.create(256, 256, false, Image.FORMAT_RGB8)
	for y in range(256):
		for x in range(256):
			var idx = y * 256 + x
			var val = map_data.layer2[idx]
			var color = colors.get(val, Color.PURPLE)
			img2.set_pixel(x, y, color)
	img2.save_png("debug_layer2_map.png")
	print("[Diagnostic] Saved debug_layer2_map.png")

	# Also do Alpha
	var img3 = Image.create(256, 256, false, Image.FORMAT_RGB8)
	for y in range(256):
		for x in range(256):
			var idx = y * 256 + x
			var val = map_data.alpha[idx]
			img3.set_pixel(x, y, Color(val, val, val))
	img3.save_png("debug_alpha_map.png")
	print("[Diagnostic] Saved debug_alpha_map.png")

	quit()
