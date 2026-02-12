extends SceneTree
func _init():
    var parser = load("res://addons/mu_tools/parsers/terrain_parser.gd").new()
    var mapping = parser.parse_mapping_file("reference/MuMain/src/bin/Data/World1/EncTerrain1.map", true)
    var att_res = parser.parse_attributes_file("reference/MuMain/src/bin/Data/World1/Terrain1.att")
    var sym = att_res.symmetry
    var l1 = mapping.layer1
    
    print("--- Tavern Area Data (115-135, 130-145) ---")
    for row in range(130, 145):
        var idx_line = []
        var sym_line = []
        for col in range(115, 135):
            var i = row * 256 + col
            idx_line.append(l1[i])
            sym_line.append(sym[i])
        print("R%d Indices: %s" % [row, str(idx_line)])
        print("R%d Sym:     %s" % [row, str(sym_line)])
    quit()
