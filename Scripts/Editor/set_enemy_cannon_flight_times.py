import unreal

FLIGHT_TIMES = (6.0, 5.0, 4.0, 3.0)
for tier, flight_time in enumerate(FLIGHT_TIMES, start=1):
    path = f'/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_{tier}'
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f'Unable to load {path}')
    profile = asset.get_editor_property('cannon_aim_profile')
    profile.projectile_flight_time = flight_time
    asset.set_editor_property('cannon_aim_profile', profile)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f'Unable to save {path}')
    print(f'ENEMY_CANNON_FLIGHT_TIME_{tier}={flight_time}')
