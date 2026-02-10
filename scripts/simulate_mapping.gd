@tool
extends SceneTree

func _init():
	var dir = DirAccess.open("res://reference/MuMain/src/bin/Data/World1")
	if not dir:
		quit(1)
		return
		
	var results = {} # idx -> [filenames]
	
	dir.list_dir_begin()
	var file_name = dir.get_next()
	while file_name != "":
		if not dir.current_is_dir():
			var lower = file_name.to_lower()
			var idx = -1
			
			var base_name = lower.get_basename()
			
			if base_name == "tilegrass01": idx = 0
			elif base_name == "tilegrass02": idx = 1
			elif base_name == "tileground01": idx = 2
			elif base_name == "tileground02": idx = 3
			elif base_name == "tileground03": idx = 4
			elif base_name == "tilewater01": idx = 5
			elif base_name == "tilewood01": idx = 6
			elif base_name == "tilerock01": idx = 7
			elif base_name == "tilerock02": idx = 8
			elif base_name == "tilerock03": idx = 9
			elif base_name == "tilerock04": idx = 10
			elif base_name == "tilerock05": idx = 11
			elif base_name == "tilerock06": idx = 12
			elif base_name == "tilerock07": idx = 13
			elif lower.begins_with("exttile"):
				pass # ext tile logic is more complex but we only check base for now
				
			if idx >= 0:
				if not results.has(idx): results[idx] = []
				results[idx].append(file_name)
		file_name = dir.get_next()
	
	print("\n=== Mapping Simulation Results ===")
	for idx in results:
		var files = results[idx]
		if files.size() > 4: # More than 4 extensions for one index suggests a collision
			# Filter out .import and different extensions for the same name
			var names = {}
			for f in files:
				if not f.ends_with(".import"):
					var base = f.get_basename()
					names[base] = true
			
			if names.keys().size() > 1:
				print("COLLISION at Index %d:" % idx)
				for n in names.keys():
					print("  - %s" % n)
	
	quit(0)
