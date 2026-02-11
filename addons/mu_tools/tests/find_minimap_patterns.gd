@tool
extends SceneTree

func _init():
	var bmd_path = "res://reference/MuMain/src/bin/Data/World1/Minimap.bmd"
	var file = FileAccess.open(bmd_path, FileAccess.READ)
	var buffer = file.get_buffer(file.get_length())
	file.close()

	print("File Size: %d" % buffer.size())
	
	var pattern = [0xfd, 0xcf, 0xab]
	var last_pos = -1
	
	for i in range(buffer.size() - 3):
		if buffer[i] == pattern[0] and buffer[i+1] == pattern[1] and buffer[i+2] == pattern[2]:
			if last_pos != -1:
				print("Pattern at %d (diff: %d)" % [i, i - last_pos])
			else:
				print("Pattern at %d" % i)
			last_pos = i
			
	quit()
