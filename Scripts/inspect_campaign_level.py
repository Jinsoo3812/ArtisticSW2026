import unreal

def inspect_campaign_level():
    level_path = "/Game/Level/Campaign_Level"
    print(f"=== Inspecting Level: {level_path} ===")
    
    world = unreal.EditorLoadingAndSavingUtils.load_map(level_path)
    if not world:
        print(f"Failed to load map: {level_path}")
        return

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors() if subsystem else unreal.EditorLevelLibrary.get_all_level_actors()
    print(f"Total Actors in level: {len(actors)}")
    
    spawn_points = []
    chests = []
    enemies = []
    enemy_ships = []
    npcs = []

    for a in actors:
        class_name = a.get_class().get_name()
        if "LootSpawnPoint" in class_name or "ChestSpawnPoint" in class_name:
            spawn_points.append(a)
        elif "StorageChest" in class_name or "Chest" in class_name:
            chests.append(a)
        elif "EnemyShip" in class_name:
            enemy_ships.append(a)
        elif "Enemy" in class_name or "Boss" in class_name:
            enemies.append(a)
        elif "NPC" in class_name or "YiSunSin" in class_name:
            npcs.append(a)

    print(f"\n--- [LootSpawnPoints / ChestSpawnPoints] Found: {len(spawn_points)} ---")
    for sp in spawn_points:
        loc = sp.get_actor_location()
        spawn_mode = sp.get_editor_property("spawn_mode") if hasattr(sp, "get_editor_property") else "N/A"
        chest_def = sp.get_editor_property("chest_definition") if hasattr(sp, "get_editor_property") else None
        random_group = sp.get_editor_property("random_group") if hasattr(sp, "get_editor_property") else None
        guard_chars = sp.get_editor_property("guard_characters") if hasattr(sp, "get_editor_property") else []
        owning_ship = sp.get_editor_property("owning_ship") if hasattr(sp, "get_editor_property") else None
        
        print(f"  * Name: {sp.get_name()} (Class: {sp.get_class().get_name()})")
        print(f"    Location: ({loc.x:.1f}, {loc.y:.1f}, {loc.z:.1f})")
        print(f"    SpawnMode: {spawn_mode}")
        print(f"    ChestDefinition: {chest_def.get_path_name() if chest_def else 'NONE'}")
        print(f"    RandomGroup: {random_group.get_path_name() if random_group else 'NONE'}")
        print(f"    GuardCharacters ({len(guard_chars)}):")
        for i, g in enumerate(guard_chars):
            print(f"      [{i}] {g.get_name() if g else 'NULL'}")
        print(f"    OwningShip: {owning_ship.get_name() if owning_ship else 'NONE'}")

    print(f"\n--- [StorageChests Placed directly in map] Found: {len(chests)} ---")
    for c in chests:
        loc = c.get_actor_location()
        chest_def = c.get_editor_property("chest_definition") if hasattr(c, "get_editor_property") else None
        print(f"  * Name: {c.get_name()}, Class: {c.get_class().get_name()}")
        print(f"    Location: ({loc.x:.1f}, {loc.y:.1f}, {loc.z:.1f})")
        print(f"    ChestDef: {chest_def.get_path_name() if chest_def else 'NONE'}")

    print(f"\n--- [Enemies / Bosses] Found: {len(enemies)} ---")
    for e in enemies:
        loc = e.get_actor_location()
        complete_node = e.get_editor_property("b_complete_story_node_on_death") if hasattr(e, "get_editor_property") else "N/A"
        node = e.get_editor_property("completed_story_node_on_death") if hasattr(e, "get_editor_property") else "N/A"
        print(f"  * Name: {e.get_name()}, Class: {e.get_class().get_name()}")
        print(f"    Location: ({loc.x:.1f}, {loc.y:.1f}, {loc.z:.1f})")
        print(f"    bCompleteOnDeath: {complete_node}, Node: {node}")

    print(f"\n--- [Enemy Ships] Found: {len(enemy_ships)} ---")
    for s in enemy_ships:
        loc = s.get_actor_location()
        sunk_def = s.get_editor_property("sunk_chest_definition") if hasattr(s, "get_editor_property") else None
        print(f"  * Name: {s.get_name()}, SunkChestDef: {sunk_def.get_path_name() if sunk_def else 'NONE'}")

    print(f"\n--- [NPCs] Found: {len(npcs)} ---")
    for n in npcs:
        print(f"  * Name: {n.get_name()}, Class: {n.get_class().get_name()}")

if __name__ == "__main__":
    inspect_campaign_level()
