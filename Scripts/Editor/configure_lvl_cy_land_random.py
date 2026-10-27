"""Apply the user-approved land counts and repair mislabeled guarded points."""
import json
from pathlib import Path
import unreal

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
points = [a for a in actors if isinstance(a, unreal.ChestSpawnPoint)
          and a.get_actor_label().startswith('BP_ChestSpawnPoint_Land_Random_')]
assert len(points) == 27, len(points)
zones = [unreal.ProgressionZone.MID1, unreal.ProgressionZone.MID2,
         unreal.ProgressionZone.MID3, unreal.ProgressionZone.FINAL]
counts = [5, 5, 4, 8]
balance = unreal.load_asset('/Game/Blueprints/Item/Data/DA_ProgressionBalance')
plans = list(balance.get_editor_property('zone_plans'))
assert len(plans) == 4
assert set(p.get_editor_property('zone') for p in plans) == set(zones)
for p in points:
    index = int(p.get_actor_label().rsplit('_', 1)[1])
    zone = zones[0 if index <= 6 else 1 if index <= 12 else 2 if index <= 17 else 3]
    assert p.get_editor_property('progression_zone') == zone
    assert p.get_editor_property('progression_kind') == unreal.ProgressionChestKind.ISLAND_RANDOM
    assert p.get_editor_property('environment') == unreal.ChestEnvironment.LAND
    assert not p.get_editor_property('guard_characters') and not p.get_editor_property('guard_spawners')
    assert not p.get_editor_property('owning_ship')
changed = []
for p in points:
    if p.get_editor_property('spawn_mode') != unreal.ChestSpawnMode.RANDOM:
        p.set_editor_property('spawn_mode', unreal.ChestSpawnMode.RANDOM)
        changed.append(p.get_actor_label())
for plan in plans:
    plan.set_editor_property('island_active_chests', counts[zones.index(plan.get_editor_property('zone'))])
balance.set_editor_property('zone_plans', plans)
assert unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False)
assert unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Level/Lvl_CY')
out = Path(unreal.Paths.project_saved_dir()) / 'LandRandomAudit' / 'applied.json'
out.write_text(json.dumps({'changed_modes': changed, 'island_active_chests': counts}, indent=2), encoding='utf-8')
unreal.log('LAND_RANDOM_CONFIG_SAVED')
