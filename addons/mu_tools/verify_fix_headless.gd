extends SceneTree

const MUTerrain = preload("res://addons/mu_tools/mu_terrain.gd")

func _init():
	print("[Verify] Starting MUTerrain instantiation...")
	var terrain = MUTerrain.new()
	
	# Point to the Data directory, NOT World1 directly, because Preflight appends World1
	terrain.data_path = "res://reference/MuMain/src/bin/Data"
	
	# Manually trigger load_world since it's usually called by _ready or main
	# We need to add it to the tree to trigger _ready if verify script was a Node, 
	# but here we are the Tree.
	root.add_child(terrain)
	
	print("[Verify] Calling load_world()...")
	terrain.load_world()
	
	print("[Verify] World loaded. Checking logs above for 'Corrected Height' messages.")
	quit()
