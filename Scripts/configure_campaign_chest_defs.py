import unreal

def configure_all_campaign_chest_defs():
    bp_chest_path = "/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_Storage_Chest.BP_Storage_Chest_C"
    chest_class = unreal.load_class(None, bp_chest_path)
    if not chest_class:
        bp_chest_path2 = "/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_StorageChest.BP_StorageChest_C"
        chest_class = unreal.load_class(None, bp_chest_path2)
    
    print(f"Loaded ChestClass: {chest_class.get_path_name() if chest_class else 'NONE'}")
    if not chest_class:
        print("ERROR: Could not load BP_Storage_Chest class!")
        return

    # All chest definition paths in both locations
    def_configs = [
        {
            "paths": [
                "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_1/DA_Chest_MidBoss1",
                "/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss1"
            ],
            "quest_tag": "Item.Quest.CipherBook",
            "req_node": unreal.EStoryNode.RECON_QUEST_ACCEPTED,
            "stop_node": unreal.EStoryNode.MIDDLE_BOSS1_DEFEATED,
            "loot_table": "/Game/Campaign/DataTable/DT_Loot_MidBoss1"
        },
        {
            "paths": [
                "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_2/DA_Chest_MidBoss2",
                "/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss2"
            ],
            "quest_tag": "Item.Quest.JapaneseCipher",
            "req_node": unreal.EStoryNode.SUPPLY_PATROL_QUEST_ACCEPTED,
            "stop_node": unreal.EStoryNode.MIDDLE_BOSS2_DEFEATED,
            "loot_table": "/Game/Campaign/DataTable/DT_Loot_MidBoss2"
        },
        {
            "paths": [
                "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_3/DA_Chest_MidBoss3",
                "/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss3"
            ],
            "quest_tag": "Item.Quest.AirRaidInfo",
            "req_node": unreal.EStoryNode.SUPPRESS_JAPANESE_FORCES_QUEST_ACCEPTED,
            "stop_node": unreal.EStoryNode.MIDDLE_BOSS3_DEFEATED,
            "loot_table": "/Game/Campaign/DataTable/DT_Loot_MidBoss3"
        },
        {
            "paths": [
                "/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_1/DA_Chest_Land_Early",
                "/Game/Campaign/DataAsset/Chest/DA_Chest_Land_Early"
            ],
            "quest_tag": None,
            "loot_table": "/Game/Campaign/DataTable/DT_Loot_EarlyTier"
        }
    ]

    for cfg in def_configs:
        for p in cfg["paths"]:
            if unreal.EditorAssetLibrary.does_asset_exist(p):
                da = unreal.EditorAssetLibrary.load_asset(p)
                if da:
                    da.set_editor_property("chest_class", chest_class)
                    if cfg.get("loot_table") and unreal.EditorAssetLibrary.does_asset_exist(cfg["loot_table"]):
                        lt = unreal.EditorAssetLibrary.load_asset(cfg["loot_table"])
                        da.set_editor_property("loot_table", lt)
                    if cfg.get("quest_tag"):
                        tag = unreal.GameplayTag()
                        tag.import_text(cfg["quest_tag"])
                        da.set_editor_property("guaranteed_quest_item_tag", tag)
                        da.set_editor_property("guaranteed_quest_item_count", 1)
                        if cfg.get("req_node"):
                            da.set_editor_property("required_story_node_for_quest_item", cfg["req_node"])
                        if cfg.get("stop_node"):
                            da.set_editor_property("b_stop_after_story_node", True)
                            da.set_editor_property("stop_after_story_node_for_quest_item", cfg["stop_node"])
                    unreal.EditorAssetLibrary.save_loaded_asset(da, only_if_is_dirty=False)
                    print(f"Successfully configured and saved: {p}")

if __name__ == "__main__":
    configure_all_campaign_chest_defs()
