extends SceneTree
func _init():
    var file = FileAccess.open("reference/MuMain/src/bin/Data/World1/Terrain1.att", FileAccess.READ)
    if not file:
        print("FAIL: FILE NOT FOUND")
        quit()
    var raw = file.get_buffer(file.get_length())
    print("FILE SIZE: ", raw.size())
    quit()
