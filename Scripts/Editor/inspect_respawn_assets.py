import unreal

def log(msg):
    unreal.log_warning("RESPAWN_INSPECT " + str(msg))

for path in [
    "/Game/Blueprints/00_Gamemode/GMBP_SWWaveGamemode",
    "/Game/Blueprints/Ship/Blueprints/BP_PlayerShip_Kelvin",
    "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin",
]:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        log(path + " MISSING")
        continue
    gc = asset.generated_class() if isinstance(asset, unreal.Blueprint) else None
    log(f"{path} class={asset.get_class().get_name()} generated={gc} parent={getattr(asset, 'parent_class', None)}")
    if gc:
        cdo = unreal.get_default_object(gc)
        comps = cdo.get_components_by_class(unreal.SceneComponent)
        for c in comps:
            log(f"  COMP {c.get_name()} class={c.get_class().get_name()} rel={c.get_editor_property('relative_location')}")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Level/Test_Level")
ws = world.get_world_settings()
log(f"WORLD gamemode_override={ws.get_editor_property('default_game_mode')}")
actors = unreal.EditorLevelLibrary.get_all_level_actors()
for a in actors:
    n = a.get_name().lower()
    if "playerstart" in n or "playership" in n or "kelvin" in n:
        log(f"ACTOR {a.get_name()} class={a.get_class().get_name()} loc={a.get_actor_location()} rot={a.get_actor_rotation()}")

log("SUBOBJECT_METHODS " + ",".join([x for x in dir(unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)) if 'subobject' in x.lower() or 'blueprint' in x.lower()]))
