import unreal

ASSET_PATH = "/Game/Blueprints/00_Gamemode/BP_PlayerRespawnPoint"
MAP_PATH = "/Game/Level/Test_Level"

asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if not asset:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.PlayerRespawnPoint)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_PlayerRespawnPoint", "/Game/Blueprints/00_Gamemode", unreal.Blueprint, factory)
    if not asset:
        raise RuntimeError("Failed to create BP_PlayerRespawnPoint")
    unreal.EditorAssetLibrary.save_loaded_asset(asset)

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
game_mode_bp = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/00_Gamemode/GMBP_SWWaveGamemode")
if not game_mode_bp:
    raise RuntimeError("GMBP_SWWaveGamemode is missing")
world.get_world_settings().set_editor_property("default_game_mode", game_mode_bp.generated_class())

actors = unreal.EditorLevelLibrary.get_all_level_actors()
for actor in list(actors):
    if actor.get_actor_label() in ("PlayerRespawnPoint_0", "PlayerRespawnPoint_1"):
        unreal.EditorLevelLibrary.destroy_actor(actor)

starts = [a for a in actors if isinstance(a, unreal.PlayerStart)]
base_loc = starts[0].get_actor_location() if starts else unreal.Vector(0, 0, 200)
bp_class = asset.generated_class()
for index, y_offset in ((0, -120.0), (1, 120.0)):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        bp_class, unreal.Vector(base_loc.x, base_loc.y + y_offset, base_loc.z), unreal.Rotator())
    actor.set_actor_label(f"PlayerRespawnPoint_{index}")
    actor.set_editor_property("player_slot", unreal.SWPlayerSlot.PLAYER0 if index == 0 else unreal.SWPlayerSlot.PLAYER1)

unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log_warning("RESPAWN_SETUP_COMPLETE")
