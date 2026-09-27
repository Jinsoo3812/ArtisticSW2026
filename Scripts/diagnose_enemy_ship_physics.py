import unreal


world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Play_Test')
if not world:
    raise RuntimeError('Failed to load Play_Test')

actors = unreal.EditorLevelLibrary.get_all_level_actors()
for actor in actors:
    class_name = actor.get_class().get_name()
    if 'EnemyShip' not in class_name and 'EnemyShip' not in actor.get_name():
        continue
    unreal.log(f'[EnemyShipDiag] Actor={actor.get_name()} Class={class_name} Location={actor.get_actor_location()} Rotation={actor.get_actor_rotation()}')
    for component in actor.get_components_by_class(unreal.PrimitiveComponent):
        component_name = component.get_name()
        if component_name not in ('BuoyancyRoot', 'ShipDeckMesh', 'DeckMesh', 'AnchorMesh', 'RepairPoint1', 'RepairPoint2', 'RepairPoint3'):
            continue
        profile = component.get_collision_profile_name()
        enabled = component.get_collision_enabled()
        local_min, local_max = component.get_local_bounds()
        extent = (local_max - local_min) * 0.5
        simulating = component.is_simulating_physics()
        unreal.log(f'[EnemyShipDiag]   Component={component_name} Profile={profile} Enabled={enabled} SimPhysics={simulating} Extent={extent} WorldLocation={component.get_component_location()}')
