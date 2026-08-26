import unreal

map_path = "/Game/Level/Test_Level"
wave_gm_path = "/Game/Blueprints/00_Gamemode/GMBP_SWWaveGamemode"
third_person_path = "/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode"

third_person = unreal.EditorAssetLibrary.load_asset(third_person_path)
if not third_person:
    raise RuntimeError("BP_ThirdPersonGameMode is missing")

world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
world.get_world_settings().set_editor_property("default_game_mode", None)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
    raise RuntimeError("Failed to save Test_Level")

wave_asset = unreal.EditorAssetLibrary.load_asset(wave_gm_path)
if wave_asset and not unreal.EditorAssetLibrary.delete_asset(wave_gm_path):
    raise RuntimeError("Failed to delete GMBP_SWWaveGamemode")

cdo = unreal.get_default_object(third_person.generated_class())
unreal.log_warning(
    "THIRD_PERSON_GAMEMODE_RESTORED default_pawn={} player_controller={} player_state={}".format(
        cdo.get_editor_property("default_pawn_class"),
        cdo.get_editor_property("player_controller_class"),
        cdo.get_editor_property("player_state_class")))
