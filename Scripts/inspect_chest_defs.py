import unreal

def inspect_chest_definitions():
    paths = [
        "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_1/DA_Chest_MidBoss1",
        "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_2/DA_Chest_MidBoss2",
        "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_3/DA_Chest_MidBoss3",
        "/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss1",
        "/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss2",
        "/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss3"
    ]
    for p in paths:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            da = unreal.EditorAssetLibrary.load_asset(p)
            chest_class = da.get_editor_property("chest_class") if da else None
            loot_table = da.get_editor_property("loot_table") if da else None
            print(f"[{p}]")
            print(f"  ChestClass: {chest_class.get_path_name() if chest_class else 'NONE'}")
            print(f"  LootTable: {loot_table.get_path_name() if loot_table else 'NONE'}")
        else:
            print(f"[{p}] DOES NOT EXIST")

if __name__ == "__main__":
    inspect_chest_definitions()
