import unreal

errors = []
bp = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/00_Gamemode/BP_PlayerRespawnPoint")
if not bp:
    errors.append("BP_PlayerRespawnPoint missing")

ship_bp = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Blueprints/BP_PlayerShip_Kelvin")
if not ship_bp:
    errors.append("Player ship BP missing")
else:
    cdo = unreal.get_default_object(ship_bp.generated_class())
    names = {c.get_name() for c in cdo.get_components_by_class(unreal.PlayerRespawnPointComponent)}
    if names != {"Player0RespawnPoint", "Player1RespawnPoint"}:
        errors.append("Ship respawn components mismatch: " + str(names))

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Level/Test_Level")
gm = world.get_world_settings().get_editor_property("default_game_mode")
if not gm or "GMBP_SWWaveGamemode" not in gm.get_path_name():
    errors.append("Test_Level game mode mismatch: " + str(gm))

points = [a for a in unreal.EditorLevelLibrary.get_all_level_actors() if isinstance(a, unreal.PlayerRespawnPoint)]
slots = {a.get_editor_property("player_slot") for a in points}
if slots != {unreal.SWPlayerSlot.PLAYER0, unreal.SWPlayerSlot.PLAYER1}:
    errors.append("Level respawn point slots mismatch: " + str(slots))

if errors:
    for error in errors:
        unreal.log_error("RESPAWN_VALIDATE " + error)
    raise RuntimeError("Respawn validation failed")
unreal.log_warning("RESPAWN_VALIDATE_OK points=2 ship_components=2 gamemode=" + gm.get_path_name())
