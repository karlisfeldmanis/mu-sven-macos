@tool
extends SceneTree

const MUOBJExporter = preload("res://addons/mu_tools/core/mu_obj_exporter.gd")

func _init():
    var exporter = MUOBJExporter.new()
    var extraction_path = "res://extracted_data/object_models/"
    
    var assets_to_export = [
        "Waterspout",
        "FireLight01", "FireLight02", "Bonfire", "Candle", "StreetLight",
        "Light01", "Light02", "Light03",
        "Tree01", "Tree02", "Tree03", "Tree04", "Tree05", "Tree06", "Tree07", 
        "Tree08", "Tree09", "Tree10", "Tree11", "Tree12", "Tree13",
        "Grass01", "Grass02", "Grass03", "Grass04", "Grass05", "Grass06", "Grass07", "Grass08",
        "Stone01", "Stone02", "Stone03", "Stone04", "Stone05",
        "StoneStatue01", "StoneStatue02", "StoneStatue03", "SteelStatue",
        "Tomb01", "Tomb02", "Tomb03",
        "DungeonGate", "TreasureDrum", "TreasureChest", "Ship",
        "StoneWall01", "StoneWall02", "StoneWall03", "StoneWall04", "StoneWall05", "StoneWall06",
        "MuWall01", "MuWall02", "MuWall03", "MuWall04",
        "SteelWall01", "SteelWall02", "SteelWall03", "SteelDoor",
        "Bridge", "Curtain", "Straw01", "Straw02",
        "Sign01", "Sign02",
        "Well01", "Well02", "Well03", "Well04",
        "Hanging",
        "House01", "House02", "House03", "House04", "House05",
        "Tent", "Stair",
        "HouseWall01", "HouseWall02", "HouseWall03", "HouseWall04", "HouseWall05", "HouseWall06",
        "HouseEtc01", "HouseEtc02", "HouseEtc03",
        "PoseBox",
        "Furniture01", "Furniture02", "Furniture03", "Furniture04", "Furniture05", "Furniture06", "Furniture07",
        "Beer01", "Beer02", "Beer03"
    ]

    print("Starting batch export of Lorencia assets...")
    
    print("Starting batch export of Lorencia assets...")
    
    var base_path = "res://reference/MuMain/src/bin/Data/"
    # We need to find the files because they might be in Object1, Object2, World1, etc.
    # We will walk the directory to find them.
    
    var found_assets = {}
    var dirs_to_scan = ["Object1", "Object2", "World1", "World2", "Ef", "Effect"]
    
    for d in dirs_to_scan:
        var path = base_path + d
        var dir = DirAccess.open(path)
        if dir:
            dir.list_dir_begin()
            var file_name = dir.get_next()
            while file_name != "":
                if not dir.current_is_dir():
                    var lower_name = file_name.to_lower()
                    if lower_name.ends_with(".bmd") or lower_name.ends_with(".ozj") or lower_name.ends_with(".ozt"):
                        var asset_base = file_name.get_basename()
                        if asset_base in assets_to_export: # Startswith check might be better for some?
                            found_assets[asset_base] = path + "/" + file_name
                        
                        # Handle case-insensitive match
                        for target in assets_to_export:
                             if target.to_lower() == asset_base.to_lower():
                                 found_assets[target] = path + "/" + file_name
                
                file_name = dir.get_next()
    
    # Export found assets
    for asset_name in assets_to_export:
        var target_file = asset_name
        if asset_name == "Waterspout": target_file = "Waterspout01" # Fix naming mismatch if needed

        var file_path = ""
        if asset_name in found_assets:
            file_path = found_assets[asset_name]
        elif target_file in found_assets:
             file_path = found_assets[target_file]
        
        if file_path != "":
            print("Exporting: %s -> %s" % [asset_name, file_path])
            
            var parser = load("res://addons/mu_tools/core/bmd_parser.gd").new()
            var success_load = false
             
            if file_path.ends_with(".bmd"):
                success_load = parser.parse_file(file_path, false) # parse_file(path, compressed?)
            else:
                 print("Skipping non-BMD file: " + file_path)
                 continue

            if success_load:
                var success = MUOBJExporter.export_bmd(parser, file_path, extraction_path)
                if success:
                    print("Successfully exported " + asset_name)
                else:
                    print("Failed to export " + asset_name)
            else:
                print("Failed to parse BMD for " + asset_name)
        else:
             print("Asset not found in scanned dirs: " + asset_name)

    print("Batch export completed.")
    quit()
            
    print("Batch export completed.")
    quit()
