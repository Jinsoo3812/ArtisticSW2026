import unreal

def configure_boss_bps_and_level_spawn_points():
    print("=== Configuring Boss Blueprints and Level Spawn Points ===")
    
    # 1. Configure Boss Blueprints
    boss_bp_configs = [
        {
            "path": "/Game/GameplayAbilitySystem/Enemy/Bosses/BP_Boss_Mid_1",
            "tag": "Enemy.Type.Boss.Mid1"
        },
        {
            "path": "/Game/GameplayAbilitySystem/Enemy/Bosses/BP_Boss_Mid_2",
            "tag": "Enemy.Type.Boss.Mid2"
        },
        {
            "path": "/Game/GameplayAbilitySystem/Enemy/Bosses/BP_Boss_Mid_3",
            "tag": "Enemy.Type.Boss.Mid3"
        },
        {
            "path": "/Game/GameplayAbilitySystem/Enemy/Bosses/BP_Boss_Final",
            "tag": "Enemy.Type.Boss.Final"
        }
    ]

    for cfg in boss_bp_configs:
        bp_path = cfg["path"]
        if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
            bp_asset = unreal.EditorAssetLibrary.load_asset(bp_path)
            cdo = unreal.get_default_object(bp_asset.generated_class())
            if cdo:
                tag = unreal.GameplayTag()
                tag.import_text(cfg["tag"])
                cdo.set_editor_property("enemy_type_tag", tag)
                unreal.EditorAssetLibrary.save_loaded_asset(bp_asset, only_if_is_dirty=False)
                print(f"Successfully set EnemyTypeTag={cfg['tag']} on CDO of {bp_path}")

    # 2. Configure Campaign_Level Spawn Points
    level_path = "/Game/Level/Campaign_Level"
    world = unreal.EditorLoadingAndSavingUtils.load_map(level_path)
    if not world:
        print(f"Failed to load map: {level_path}")
        return

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors() if subsystem else unreal.EditorLevelLibrary.get_all_level_actors()

    tag_mid1 = unreal.GameplayTag(); tag_mid1.import_text("Enemy.Type.Boss.Mid1")
    tag_mid2 = unreal.GameplayTag(); tag_mid2.import_text("Enemy.Type.Boss.Mid2")
    tag_mid3 = unreal.GameplayTag(); tag_mid3.import_text("Enemy.Type.Boss.Mid3")

    tag_cipher_book = unreal.GameplayTag(); tag_cipher_book.import_text("Item.Quest.CipherBook")
    tag_jp_cipher = unreal.GameplayTag(); tag_jp_cipher.import_text("Item.Quest.JapaneseCipher")
    tag_air_raid = unreal.GameplayTag(); tag_air_raid.import_text("Item.Quest.AirRaidInfo")

    for a in actors:
        name = a.get_name()
        # Check enemy instances directly in level to assign EnemyTypeTag as well
        if "BP_Boss_Mid_1" in name or "Boss_Mid_1" in name:
            a.set_editor_property("enemy_type_tag", tag_mid1)
            print(f"Set EnemyTypeTag on level actor: {name}")
        elif "BP_Boss_Mid_2" in name or "Boss_Mid_2" in name:
            a.set_editor_property("enemy_type_tag", tag_mid2)
            print(f"Set EnemyTypeTag on level actor: {name}")
        elif "BP_Boss_Mid_3" in name or "Boss_Mid_3" in name:
            a.set_editor_property("enemy_type_tag", tag_mid3)
            print(f"Set EnemyTypeTag on level actor: {name}")

        if "ChestSpawnPoint" in name:
            guards = a.get_editor_property("guard_characters") if hasattr(a, "get_editor_property") else []
            guard_names = [g.get_name() for g in guards if g]
            
            is_mid1 = any("Mid_1" in gn for gn in guard_names)
            is_mid2 = any("Mid_2" in gn for gn in guard_names)
            is_mid3 = any("Mid_3" in gn for gn in guard_names)

            if is_mid1:
                try: a.set_editor_property("is_boss_chest", True)
                except: a.set_editor_property("b_is_boss_chest", True)
                a.set_editor_property("required_boss_tag", tag_mid1)
                a.set_editor_property("guaranteed_boss_quest_item_tag", tag_cipher_book)
                a.set_editor_property("guaranteed_boss_quest_item_count", 1)
                print(f"Configured MidBoss 1 Chest Spawn Point: {name} (RequiredBossTag=Enemy.Type.Boss.Mid1)")
            elif is_mid2:
                try: a.set_editor_property("is_boss_chest", True)
                except: a.set_editor_property("b_is_boss_chest", True)
                a.set_editor_property("required_boss_tag", tag_mid2)
                a.set_editor_property("guaranteed_boss_quest_item_tag", tag_jp_cipher)
                a.set_editor_property("guaranteed_boss_quest_item_count", 1)
                print(f"Configured MidBoss 2 Chest Spawn Point: {name} (RequiredBossTag=Enemy.Type.Boss.Mid2)")
            elif is_mid3:
                try: a.set_editor_property("is_boss_chest", True)
                except: a.set_editor_property("b_is_boss_chest", True)
                a.set_editor_property("required_boss_tag", tag_mid3)
                a.set_editor_property("guaranteed_boss_quest_item_tag", tag_air_raid)
                a.set_editor_property("guaranteed_boss_quest_item_count", 1)
                print(f"Configured MidBoss 3 Chest Spawn Point: {name} (RequiredBossTag=Enemy.Type.Boss.Mid3)")

    unreal.EditorLoadingAndSavingUtils.save_map(world, level_path)
    print(f"Successfully saved level: {level_path}")

if __name__ == "__main__":
    configure_boss_bps_and_level_spawn_points()
