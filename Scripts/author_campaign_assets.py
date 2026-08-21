"""
Campaign Content Assets Authoring Script
Generates all necessary DataTables, DataAssets, and Blueprints for the Campaign system in Unreal Engine.
"""

import json
import unreal

def get_or_create_package(folder, asset_name, asset_class, factory):
    asset_path = f"{folder}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)
    
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    new_asset = asset_tools.create_asset(asset_name, folder, asset_class, factory)
    if new_asset is None:
        raise RuntimeError(f"Failed to create asset: {asset_path}")
    return new_asset

def create_data_table(folder, table_name, row_struct, rows_data):
    asset_path = f"{folder}/{table_name}"
    unreal.EditorAssetLibrary.make_directory(folder)
    
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    
    dt = get_or_create_package(folder, table_name, unreal.DataTable, factory)
    
    json_rows = []
    for row_name, data in rows_data.items():
        row_dict = {"Name": row_name}
        row_dict.update(data)
        json_rows.append(row_dict)
    
    json_str = json.dumps(json_rows)
    success = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json_str)
    if not success:
        unreal.log_warning(f"Failed to fill JSON in {asset_path}")
    
    unreal.EditorAssetLibrary.save_loaded_asset(dt, only_if_is_dirty=False)
    unreal.log(f"[Asset Created] DataTable: {asset_path}")
    return dt

def create_chest_definition(folder, asset_name, loot_table, roll_count, slot_count, col_count,
                            quest_tag=None, quest_count=1, req_node=None, stop_node=None, chest_class=None):
    unreal.EditorAssetLibrary.make_directory(folder)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.ChestDefinition)
    
    da = get_or_create_package(folder, asset_name, unreal.ChestDefinition, factory)
    
    if chest_class:
        da.set_editor_property("chest_class", chest_class)
    if loot_table:
        da.set_editor_property("loot_table", loot_table)
    
    da.set_editor_property("roll_count", roll_count)
    da.set_editor_property("slot_count", slot_count)
    da.set_editor_property("column_count", col_count)
    
    if quest_tag:
        tag = unreal.GameplayTag()
        tag.import_text(quest_tag)
        da.set_editor_property("guaranteed_quest_item_tag", tag)
        da.set_editor_property("guaranteed_quest_item_count", quest_count)
        if req_node:
            da.set_editor_property("required_story_node_for_quest_item", req_node)
        if stop_node:
            da.set_editor_property("b_stop_after_story_node", True)
            da.set_editor_property("stop_after_story_node_for_quest_item", stop_node)
    
    unreal.EditorAssetLibrary.save_loaded_asset(da, only_if_is_dirty=False)
    unreal.log(f"[Asset Created] ChestDefinition: {folder}/{asset_name}")
    return da

def create_random_group(folder, asset_name, chest_def, spawn_count):
    unreal.EditorAssetLibrary.make_directory(folder)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.RandomChestGroup)
    
    rg = get_or_create_package(folder, asset_name, unreal.RandomChestGroup, factory)
    rg.set_editor_property("chest_definition", chest_def)
    rg.set_editor_property("spawn_count", spawn_count)
    
    unreal.EditorAssetLibrary.save_loaded_asset(rg, only_if_is_dirty=False)
    unreal.log(f"[Asset Created] RandomChestGroup: {folder}/{asset_name}")
    return rg

def create_blueprint(folder, asset_name, parent_class):
    asset_path = f"{folder}/{asset_name}"
    unreal.EditorAssetLibrary.make_directory(folder)
    
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)
    
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, folder, unreal.Blueprint, factory
    )
    if bp:
        unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
        unreal.log(f"[Asset Created] Blueprint: {asset_path}")
    return bp

def create_dialogue_data(folder, asset_name, rules):
    unreal.EditorAssetLibrary.make_directory(folder)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.NPCDialogueData)
    
    da = get_or_create_package(folder, asset_name, unreal.NPCDialogueData, factory)
    
    dialogue_rules = []
    for r in rules:
        rule = unreal.NPCDialogueRule()
        rule.set_editor_property("rule_id", r.get("rule_id", "Rule"))
        rule.set_editor_property("priority", r.get("priority", 0))
        
        if "required_story_nodes" in r:
            rule.set_editor_property("required_story_nodes", r["required_story_nodes"])
        if "blocked_story_nodes" in r:
            rule.set_editor_property("blocked_story_nodes", r["blocked_story_nodes"])
        if "b_complete_story_node" in r:
            rule.set_editor_property("b_complete_story_node", r["b_complete_story_node"])
            rule.set_editor_property("story_node_to_complete", r.get("story_node_to_complete", unreal.EStoryNode.GAME_STARTED))
            rule.set_editor_property("b_hide_after_story_completion", r.get("b_hide_after_story_completion", True))
        if "required_items" in r:
            req_items = []
            for item in r["required_items"]:
                stack = unreal.CraftingItemStack()
                stack.set_editor_property("item_tag", unreal.GameplayTag(item["item_tag"]))
                stack.set_editor_property("quantity", item["quantity"])
                req_items.append(stack)
            rule.set_editor_property("required_items", req_items)
        
        lines = []
        for l in r.get("lines", []):
            line = unreal.NPCDialogueLine()
            line.set_editor_property("line_id", l.get("line_id", "Line"))
            line.set_editor_property("text", unreal.Text(l.get("text", "")))
            lines.append(line)
        rule.set_editor_property("lines", lines)
        
        dialogue_rules.append(rule)
        
    da.set_editor_property("rules", dialogue_rules)
    unreal.EditorAssetLibrary.save_loaded_asset(da, only_if_is_dirty=False)
    unreal.log(f"[Asset Created] NPCDialogueData: {folder}/{asset_name}")
    return da

def main():
    unreal.log("=== Starting Campaign Assets Authoring ===")
    
    # ----------------------------------------------------
    # 1. Data Tables
    # ----------------------------------------------------
    row_struct = unreal.load_object(None, "/Script/ClassFeature.ChestInitialLootRow")
    dt_folder = "/Game/Campaign/DataTable"
    
    dt_early = create_data_table(dt_folder, "DT_Loot_EarlyTier", row_struct, {
        "Wood": {"ItemTag": {"TagName": "Item.Material.Wood"}, "MinCount": 3, "MaxCount": 6, "Weight": 100.0},
        "Iron": {"ItemTag": {"TagName": "Item.Material.Iron"}, "MinCount": 2, "MaxCount": 4, "Weight": 80.0},
        "Gunpowder": {"ItemTag": {"TagName": "Item.Material.Gunpowder"}, "MinCount": 1, "MaxCount": 2, "Weight": 40.0},
        "Potion": {"ItemTag": {"TagName": "Item.Consumable.Potion"}, "MinCount": 1, "MaxCount": 2, "Weight": 50.0},
    })
    
    dt_mid = create_data_table(dt_folder, "DT_Loot_MidTier", row_struct, {
        "HighWood": {"ItemTag": {"TagName": "Item.Material.HighWood"}, "MinCount": 4, "MaxCount": 8, "Weight": 100.0},
        "HighIron": {"ItemTag": {"TagName": "Item.Material.HighIron"}, "MinCount": 3, "MaxCount": 6, "Weight": 80.0},
        "Gunpowder": {"ItemTag": {"TagName": "Item.Material.Gunpowder"}, "MinCount": 3, "MaxCount": 5, "Weight": 70.0},
        "Cannonball": {"ItemTag": {"TagName": "Item.Ammo.Cannon"}, "MinCount": 5, "MaxCount": 10, "Weight": 60.0},
    })

    dt_late = create_data_table(dt_folder, "DT_Loot_LateTier", row_struct, {
        "HighIron": {"ItemTag": {"TagName": "Item.Material.HighIron"}, "MinCount": 5, "MaxCount": 10, "Weight": 100.0},
        "HeavyCannon": {"ItemTag": {"TagName": "Item.Ammo.HeavyCannon"}, "MinCount": 5, "MaxCount": 10, "Weight": 80.0},
        "Gold": {"ItemTag": {"TagName": "Item.Currency.Gold"}, "MinCount": 100, "MaxCount": 200, "Weight": 90.0},
    })

    dt_sunk = create_data_table(dt_folder, "DT_Loot_EnemyShipSunk", row_struct, {
        "WoodDebris": {"ItemTag": {"TagName": "Item.Material.Wood"}, "MinCount": 5, "MaxCount": 10, "Weight": 100.0},
        "IronDebris": {"ItemTag": {"TagName": "Item.Material.Iron"}, "MinCount": 3, "MaxCount": 6, "Weight": 80.0},
        "Cannonball": {"ItemTag": {"TagName": "Item.Ammo.Cannon"}, "MinCount": 4, "MaxCount": 8, "Weight": 60.0},
    })

    dt_mb1 = create_data_table(dt_folder, "DT_Loot_MidBoss1", row_struct, {
        "Bonus_HighIron": {"ItemTag": {"TagName": "Item.Material.HighIron"}, "MinCount": 3, "MaxCount": 5, "Weight": 100.0},
        "Bonus_Gold": {"ItemTag": {"TagName": "Item.Currency.Gold"}, "MinCount": 50, "MaxCount": 100, "Weight": 100.0},
    })

    dt_mb2 = create_data_table(dt_folder, "DT_Loot_MidBoss2", row_struct, {
        "Bonus_HighWood": {"ItemTag": {"TagName": "Item.Material.HighWood"}, "MinCount": 4, "MaxCount": 6, "Weight": 100.0},
        "Bonus_Gold": {"ItemTag": {"TagName": "Item.Currency.Gold"}, "MinCount": 80, "MaxCount": 150, "Weight": 100.0},
    })

    dt_mb3 = create_data_table(dt_folder, "DT_Loot_MidBoss3", row_struct, {
        "Bonus_HeavyCannon": {"ItemTag": {"TagName": "Item.Ammo.HeavyCannon"}, "MinCount": 5, "MaxCount": 10, "Weight": 100.0},
        "Bonus_Gold": {"ItemTag": {"TagName": "Item.Currency.Gold"}, "MinCount": 100, "MaxCount": 200, "Weight": 100.0},
    })

    dt_cipher = create_data_table(dt_folder, "DT_Loot_CipherBookSub", row_struct, {
        "Bonus_Supplies": {"ItemTag": {"TagName": "Item.Material.Supplies"}, "MinCount": 2, "MaxCount": 4, "Weight": 100.0},
        "Bonus_Potion": {"ItemTag": {"TagName": "Item.Consumable.Potion"}, "MinCount": 1, "MaxCount": 2, "Weight": 80.0},
    })

    # ----------------------------------------------------
    # 2. Blueprints (Base Actors)
    # ----------------------------------------------------
    bp_folder = "/Game/Campaign/Blueprints"
    storage_class = unreal.load_class(None, "/Script/ClassFeature.StorageChest")
    spawnpoint_class = unreal.load_class(None, "/Script/ClassFeature.ChestSpawnPoint")
    cond_spawner_class = unreal.load_class(None, "/Script/Story.StoryConditionalSpawner")
    npc_class = unreal.load_class(None, "/Script/NPCDialogue.NPCCharacter")

    bp_chest = create_blueprint(bp_folder, "BP_StorageChest", storage_class)
    bp_spawnpoint = create_blueprint(bp_folder, "BP_ChestSpawnPoint", spawnpoint_class)
    bp_story_spawner = create_blueprint(bp_folder, "BP_StoryConditionalSpawner", cond_spawner_class)
    bp_npc_yisunsin = create_blueprint(bp_folder, "BP_NPC_YiSunSin", npc_class)
    bp_npc_helper = create_blueprint(bp_folder, "BP_NPC_BaseHelper", npc_class)

    chest_actor_class = bp_chest.generated_class() if bp_chest else storage_class

    # ----------------------------------------------------
    # 3. Chest Definitions (DataAssets)
    # ----------------------------------------------------
    chest_da_folder = "/Game/Campaign/DataAsset/Chest"
    
    da_land_early = create_chest_definition(chest_da_folder, "DA_Chest_Land_Early", dt_early, 3, 5, 4, chest_class=chest_actor_class)
    da_ocean_early = create_chest_definition(chest_da_folder, "DA_Chest_Ocean_Early", dt_early, 3, 5, 4, chest_class=chest_actor_class)
    da_land_mid = create_chest_definition(chest_da_folder, "DA_Chest_Land_Mid", dt_mid, 3, 5, 4, chest_class=chest_actor_class)
    da_ocean_mid = create_chest_definition(chest_da_folder, "DA_Chest_Ocean_Mid", dt_mid, 3, 5, 4, chest_class=chest_actor_class)
    da_land_late = create_chest_definition(chest_da_folder, "DA_Chest_Land_Late", dt_late, 4, 6, 4, chest_class=chest_actor_class)
    da_ocean_late = create_chest_definition(chest_da_folder, "DA_Chest_Ocean_Late", dt_late, 4, 6, 4, chest_class=chest_actor_class)
    da_ship_deck = create_chest_definition(chest_da_folder, "DA_Chest_ShipDeck", dt_mid, 3, 5, 4, chest_class=chest_actor_class)
    da_ship_sunk = create_chest_definition(chest_da_folder, "DA_Chest_ShipSunk", dt_sunk, 4, 6, 4, chest_class=chest_actor_class)

    # Story Boss Chests
    da_mb1 = create_chest_definition(chest_da_folder, "DA_Chest_MidBoss1", dt_mb1, 2, 6, 4,
                                    quest_tag="Item.Quest.CipherBook", quest_count=1,
                                    req_node=unreal.EStoryNode.RECON_QUEST_ACCEPTED,
                                    stop_node=unreal.EStoryNode.MIDDLE_BOSS1_DEFEATED,
                                    chest_class=chest_actor_class)

    da_mb2 = create_chest_definition(chest_da_folder, "DA_Chest_MidBoss2", dt_mb2, 2, 6, 4,
                                    quest_tag="Item.Quest.JapaneseCipher", quest_count=1,
                                    req_node=unreal.EStoryNode.SUPPLY_PATROL_QUEST_ACCEPTED,
                                    stop_node=unreal.EStoryNode.MIDDLE_BOSS2_DEFEATED,
                                    chest_class=chest_actor_class)

    da_mb3 = create_chest_definition(chest_da_folder, "DA_Chest_MidBoss3", dt_mb3, 2, 6, 4,
                                    quest_tag="Item.Quest.AirRaidInfo", quest_count=1,
                                    req_node=unreal.EStoryNode.SUPPRESS_JAPANESE_FORCES_QUEST_ACCEPTED,
                                    stop_node=unreal.EStoryNode.MIDDLE_BOSS3_DEFEATED,
                                    chest_class=chest_actor_class)

    da_cipher = create_chest_definition(chest_da_folder, "DA_Chest_CipherBook", dt_cipher, 2, 4, 4,
                                       quest_tag="Item.Quest.CipherBook", quest_count=1,
                                       req_node=unreal.EStoryNode.RECON_QUEST_ACCEPTED,
                                       stop_node=unreal.EStoryNode.CIPHER_BOOK_ACQUIRED,
                                       chest_class=chest_actor_class)

    # ----------------------------------------------------
    # 4. Crafting Recipes
    # ----------------------------------------------------
    crafting_folder = "/Game/Blueprints/Item"
    crafting_rows = {
        "Recipe_DecipherCipher": {
            "ResultItemTag": {"TagName": "Item.Quest.DecipheredCipher"},
            "ResultQuantity": 1,
            "RequiredRecipeItemTag": {"TagName": "None"},
            "bConsumeRecipeItem": False,
            "bEnabled": True,
            "SortOrder": 100,
            "Ingredients": [
                {"ItemTag": {"TagName": "Item.Quest.CipherBook"}, "Quantity": 1},
                {"ItemTag": {"TagName": "Item.Quest.JapaneseCipher"}, "Quantity": 1}
            ]
        }
    }
    try:
        crafting_struct = unreal.load_object(None, "/Script/ArtisticSWCore.CraftingRecipeRow")
        if crafting_struct:
            create_data_table(crafting_folder, "DT_CraftingRecipes", crafting_struct, crafting_rows)
        else:
            unreal.log_warning("[Crafting] FCraftingRecipeRow struct not found; skipping DT_CraftingRecipes authoring.")
    except Exception as e:
        unreal.log_warning(f"[Crafting] Exception creating crafting data table: {e}")

    # ----------------------------------------------------
    # 5. Random Groups
    # ----------------------------------------------------
    create_random_group(chest_da_folder, "DA_RandomGroup_Land_Early", da_land_early, 2)
    create_random_group(chest_da_folder, "DA_RandomGroup_Ocean_Early", da_ocean_early, 2)
    create_random_group(chest_da_folder, "DA_RandomGroup_Land_Mid", da_land_mid, 3)
    create_random_group(chest_da_folder, "DA_RandomGroup_Ocean_Mid", da_ocean_mid, 2)
    create_random_group(chest_da_folder, "DA_RandomGroup_Land_Late", da_land_late, 2)
    create_random_group(chest_da_folder, "DA_RandomGroup_Ocean_Late", da_ocean_late, 2)

    # ----------------------------------------------------
    # 6. Dialogue DataAssets (Single Unified YiSunSin Dialogue)
    # ----------------------------------------------------
    dialogue_folder = "/Game/Campaign/DataAsset/Dialogue"

    yisunsin_rules = [
        {
            "rule_id": "Rule_ReconQuest",
            "priority": 200,
            "required_story_nodes": [unreal.EStoryNode.GAME_STARTED],
            "blocked_story_nodes": [unreal.EStoryNode.RECON_QUEST_ACCEPTED],
            "b_complete_story_node": True,
            "story_node_to_complete": unreal.EStoryNode.RECON_QUEST_ACCEPTED,
            "lines": [
                {"line_id": "L1", "text": "왜군의 움직임이 심상치 않소. 전방 해역을 정찰하고 적 암호 해독서를 확보하며 적 선봉장(중간보스 1)을 격파하시오."}
            ]
        },
        {
            "rule_id": "Rule_SupplyPatrol",
            "priority": 210,
            "required_story_nodes": [unreal.EStoryNode.MIDDLE_BOSS1_DEFEATED],
            "blocked_story_nodes": [unreal.EStoryNode.SUPPLY_PATROL_QUEST_ACCEPTED],
            "b_complete_story_node": True,
            "story_node_to_complete": unreal.EStoryNode.SUPPLY_PATROL_QUEST_ACCEPTED,
            "lines": [
                {"line_id": "L1", "text": "적 선봉장을 격파했군! 이제 놈들의 보급로를 차단하고 중간보스 2를 격파하여 일본군 암호를 노획하시오."}
            ]
        },
        {
            "rule_id": "Rule_DecipherQuest",
            "priority": 220,
            "required_story_nodes": [unreal.EStoryNode.MIDDLE_BOSS2_DEFEATED],
            "blocked_story_nodes": [unreal.EStoryNode.SUPPRESS_JAPANESE_FORCES_QUEST_ACCEPTED],
            "b_complete_story_node": True,
            "story_node_to_complete": unreal.EStoryNode.DECIPHER_QUEST_ACCEPTED,
            "lines": [
                {"line_id": "L1", "text": "일본군 암호를 노획했군! 제작대(작업대)에서 암호 해독서와 조합하여 [해독된 암호]를 제작해 오시오."}
            ]
        },
        {
            "rule_id": "Rule_SuppressForces",
            "priority": 230,
            "required_story_nodes": [unreal.EStoryNode.MIDDLE_BOSS2_DEFEATED],
            "blocked_story_nodes": [unreal.EStoryNode.SUPPRESS_JAPANESE_FORCES_QUEST_ACCEPTED],
            "required_items": [
                {"item_tag": "Item.Quest.DecipheredCipher", "quantity": 1}
            ],
            "b_complete_story_node": True,
            "story_node_to_complete": unreal.EStoryNode.SUPPRESS_JAPANESE_FORCES_QUEST_ACCEPTED,
            "lines": [
                {"line_id": "L1", "text": "훌륭하오! 해독된 정보에 따르면 왜군 본대의 공습이 임박했소. 중간보스 3을 먼저 저지하시오."}
            ]
        },
        {
            "rule_id": "Rule_UldolmokBattle",
            "priority": 240,
            "required_story_nodes": [unreal.EStoryNode.MIDDLE_BOSS3_DEFEATED],
            "blocked_story_nodes": [unreal.EStoryNode.ULDOLMOK_BATTLE_QUEST_ACCEPTED],
            "b_complete_story_node": True,
            "story_node_to_complete": unreal.EStoryNode.ULDOLMOK_BATTLE_QUEST_ACCEPTED,
            "lines": [
                {"line_id": "L1", "text": "적의 총공세가 시작되었소. 울돌목의 좁은 해협으로 적 최종 함대를 유인해 격멸합시다!"}
            ]
        },
        {
            "rule_id": "Rule_Ending",
            "priority": 250,
            "required_story_nodes": [unreal.EStoryNode.FINAL_BOSS_DEFEATED],
            "blocked_story_nodes": [unreal.EStoryNode.ENDING_DIALOGUE_COMPLETED],
            "b_complete_story_node": True,
            "story_node_to_complete": unreal.EStoryNode.ENDING_DIALOGUE_COMPLETED,
            "lines": [
                {"line_id": "L1", "text": "신에게는 아직 12척의 배가 남아있었소... 왜적을 완파하고 조선의 바다를 지켜내었소!"}
            ]
        },
        {
            "rule_id": "Rule_Ambient",
            "priority": 0,
            "lines": [
                {"line_id": "L1", "text": "바다를 지키는 일은 한 치의 방심도 허용되지 않소."}
            ]
        }
    ]
    try:
        create_dialogue_data(dialogue_folder, "DA_YiSunSinDialogue", yisunsin_rules)
    except Exception as e:
        unreal.log_error(f"[Dialogue] Failed to create DA_YiSunSinDialogue: {e}")

    # Test Dialogue asset for existing automation test
    test_npc_folder = "/Game/New/NPC/Data"
    try:
        create_dialogue_data(test_npc_folder, "DA_TestNPCDialogue", yisunsin_rules)
    except Exception as e:
        unreal.log_warning(f"[Dialogue] Failed to create DA_TestNPCDialogue: {e}")

    unreal.log("=== Campaign Assets Authoring Completed Successfully! ===")

try:
    main()
except Exception as e:
    unreal.log_error(f"Critical error in author_campaign_assets.py: {e}")