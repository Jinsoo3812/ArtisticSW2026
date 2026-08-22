import unreal

def apply_boss_chest_settings_to_level():
    level_path = "/Game/Level/Campaign_Level"
    print(f"=== Applying Boss Chest Settings to {level_path} ===")
    
    world = unreal.EditorLoadingAndSavingUtils.load_map(level_path)
    if not world:
        print(f"Failed to load map: {level_path}")
        return

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors() if subsystem else unreal.EditorLevelLibrary.get_all_level_actors()

    tag_cipher_book = unreal.GameplayTag()
    tag_cipher_book.import_text("Item.Quest.CipherBook")

    tag_jp_cipher = unreal.GameplayTag()
    tag_jp_cipher.import_text("Item.Quest.JapaneseCipher")

    tag_air_raid = unreal.GameplayTag()
    tag_air_raid.import_text("Item.Quest.AirRaidInfo")

    for a in actors:
        name = a.get_name()
        if "ChestSpawnPoint" in name:
            guards = a.get_editor_property("guard_characters") if hasattr(a, "get_editor_property") else []
            guard_names = [g.get_name() for g in guards if g]
            
            is_mid1 = any("Mid_1" in gn for gn in guard_names)
            is_mid2 = any("Mid_2" in gn for gn in guard_names)
            is_mid3 = any("Mid_3" in gn for gn in guard_names)

            prop_name = "is_boss_chest" if hasattr(a, "get_editor_property") and "is_boss_chest" in dir(a) else "b_is_boss_chest"
            
            if is_mid1:
                try:
                    a.set_editor_property("is_boss_chest", True)
                except:
                    a.set_editor_property("b_is_boss_chest", True)
                a.set_editor_property("guaranteed_boss_quest_item_tag", tag_cipher_book)
                a.set_editor_property("guaranteed_boss_quest_item_count", 1)
                print(f"Configured MidBoss 1 Chest Spawn Point: {name} with Item.Quest.CipherBook")
            elif is_mid2:
                try:
                    a.set_editor_property("is_boss_chest", True)
                except:
                    a.set_editor_property("b_is_boss_chest", True)
                a.set_editor_property("guaranteed_boss_quest_item_tag", tag_jp_cipher)
                a.set_editor_property("guaranteed_boss_quest_item_count", 1)
                print(f"Configured MidBoss 2 Chest Spawn Point: {name} with Item.Quest.JapaneseCipher")
            elif is_mid3:
                try:
                    a.set_editor_property("is_boss_chest", True)
                except:
                    a.set_editor_property("b_is_boss_chest", True)
                a.set_editor_property("guaranteed_boss_quest_item_tag", tag_air_raid)
                a.set_editor_property("guaranteed_boss_quest_item_count", 1)
                print(f"Configured MidBoss 3 Chest Spawn Point: {name} with Item.Quest.AirRaidInfo")

    unreal.EditorLoadingAndSavingUtils.save_map(world, level_path)
    print(f"Successfully saved level: {level_path}")

if __name__ == "__main__":
    apply_boss_chest_settings_to_level()
