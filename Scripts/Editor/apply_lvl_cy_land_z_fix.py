"""Apply measured vertical-only placement changes and the requested deletion of 17."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'LandRandomAudit'
plan = json.loads((out / 'z_fix_plan.json').read_text(encoding='utf-8'))
world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
points = {p.get_actor_label(): p for p in sub.get_all_level_actors()
          if isinstance(p, unreal.ChestSpawnPoint) and p.get_actor_label().startswith('BP_ChestSpawnPoint_Land_Random_')}
assert len(points) == 27
changes = []
for row in plan:
    p = points[row['label']]
    if row['label'].endswith('_17'):
        continue
    fix = row['z_fix']
    assert fix['status'] in ['raise', 'unchanged']
    before = p.get_actor_location()
    assert abs(before.x - row['point_location'][0]) < 0.01
    assert abs(before.y - row['point_location'][1]) < 0.01
    assert abs(before.z - fix['old_point_z']) < 0.01
    assert fix['new_point_z'] >= before.z
    assert fix['new_ground_clearance'] >= fix['old_ground_clearance']
    if fix['status'] == 'raise':
        p.set_editor_property('ground_clearance', fix['new_ground_clearance'])
        if fix['new_point_z'] > before.z:
            p.set_actor_location(unreal.Vector(before.x, before.y, fix['new_point_z']), False, True)
        after = p.get_actor_location()
        assert after.x == before.x and after.y == before.y
        changes.append({'label': row['label'], **fix})
assert sub.destroy_actor(points['BP_ChestSpawnPoint_Land_Random_17'])
balance = unreal.load_asset('/Game/Blueprints/Item/Data/DA_ProgressionBalance')
plans = list(balance.get_editor_property('zone_plans'))
for p in plans:
    if p.get_editor_property('zone') == unreal.ProgressionZone.MID3:
        assert p.get_editor_property('island_active_chests') == 4
        p.set_editor_property('island_active_chests', 3)
balance.set_editor_property('zone_plans', plans)
assert unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False)
assert unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Level/Lvl_CY')
(out / 'z_fix_applied.json').write_text(json.dumps({'changes': changes, 'deleted': 17,
    'active_counts': [5, 5, 3, 8]}, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('LAND_RANDOM_Z_FIX_SAVED')
