import unreal

errors = []
if unreal.EditorAssetLibrary.does_asset_exist("/Game/Blueprints/00_Gamemode/GMBP_SWWaveGamemode"):
    errors.append("wave GameMode asset still exists")
world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Level/Test_Level")
if world.get_world_settings().get_editor_property("default_game_mode") is not None:
    errors.append("Test_Level still overrides GameMode")
bp = unreal.EditorAssetLibrary.load_asset("/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode")
if not bp:
    errors.append("BP_ThirdPersonGameMode missing")
else:
    cdo = unreal.get_default_object(bp.generated_class())
    if not isinstance(cdo, unreal.MultiGameMode):
        errors.append("BP_ThirdPersonGameMode does not inherit MultiGameMode rules")
    if not cdo.get_editor_property("default_pawn_class"):
        errors.append("DefaultPawnClass missing")
    if not cdo.get_editor_property("player_controller_class"):
        errors.append("PlayerControllerClass missing")
    if not cdo.get_editor_property("player_state_class"):
        errors.append("PlayerStateClass missing")
if errors:
    for error in errors:
        unreal.log_error("GAME_MODE_VALIDATE " + error)
    raise RuntimeError("GameMode restore validation failed")
unreal.log_warning("GAME_MODE_VALIDATE_OK")
