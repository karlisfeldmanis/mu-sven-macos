@tool
extends SceneTree

const MUTextureLoader = preload("res://addons/mu_tools/rendering/texture_loader.gd")

func _init():
	print("\n======================================================================")
	print("MINIMAP EXPORTER")
	print("======================================================================")
	
	var world_id = 1
	var ozt_path = "res://reference/MuMain/src/bin/Data/World%d/mini_map.OZT" % world_id
	var output_path = "mini_map_high_res.png"
	
	print("Loading %s..." % ozt_path)
	var texture = MUTextureLoader.load_mu_texture(ozt_path)
	
	if not texture:
		print("FAILED to load minimap texture")
		quit()
		return
		
	var image = texture.get_image()
	print("Minimap Dimensions: %dx%d" % [image.get_width(), image.get_height()])
	
	image.save_png("mini_map_high_res.png")
	
	# Create High-Quality Fountain Crop (512x512 pixels from source)
	# This covers 128 MU Tiles (1/4th of the map) at 4px per tile
	var fountain_rect = Rect2i(512 - 256, 512 - 256, 512, 512)
	var fountain_img = image.get_region(fountain_rect)
	
	# We'll save this raw 512x512 for the AI tool
	fountain_img.save_png("mini_map_fountain_raw.png")
	
	# And a sharp 2048x2048 version for immediate viewing
	var zoom_img = fountain_img.duplicate()
	zoom_img.resize(2048, 2048, Image.INTERPOLATE_LANCZOS)
	
	var err = zoom_img.save_png("mini_map_city_zoom.png")
	if err == OK:
		print("✓ HIGH-RES CITY ZOOM SAVED: mini_map_city_zoom.png")
		print("✓ RAW FOUNTAIN CROP SAVED: mini_map_fountain_raw.png")
	else:
		print("FAILED to save City Zoom PNG: %d" % err)
	
	quit()
