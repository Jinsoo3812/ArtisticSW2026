"""Read-only saved-level and loot asset inventory. Run with Unreal Python."""
import json
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.project_saved_dir()) / 'LandRandomAudit'
OUT.mkdir(parents=True, exist_ok=True)

def enc(v):
    if v is None or isinstance(v, (str, int, float, bool)):
        return v
    if isinstance(v, unreal.Actor):
        return {'name': v.get_name(), 'label': v.get_actor_label(), 'path': v.get_path_name()}
    if isinstance(v, unreal.Object):
        return v.get_path_name()
    if isinstance(v, (unreal.Array, list, tuple)):
        return [enc(x) for x in v]
    if isinstance(v, unreal.StructBase):
        return v.export_text()
    return str(v)

def fields(obj, names):
    result = {}
    for name in names:
        try:
            result[name] = enc(obj.get_editor_property(name))
        except Exception as exc:
            result[name] = {'error': str(exc)}
    return result

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
if not world:
    raise RuntimeError('Lvl_CY failed to load')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
report = {'world': world.get_path_name(), 'points': [], 'managers': [], 'ships': [], 'assets': []}
point_fields = ['enabled', 'point_weight', 'environment', 'spawn_mode', 'progression_zone',
                'progression_kind', 'chest_class_override', 'random_group', 'chest_definition',
                'guard_characters', 'guard_spawners', 'owning_ship', 'align_chest_bottom_to_ground',
                'ground_clearance', 'ground_trace_up_distance', 'ground_trace_down_distance']
for actor in actors:
    cls = actor.get_class().get_name()
    if isinstance(actor, unreal.ChestSpawnPoint):
        item = fields(actor, point_fields)
        item.update(name=actor.get_name(), label=actor.get_actor_label(), class_name=cls,
                    folder=str(actor.get_folder_path()), parent=enc(actor.get_attach_parent_actor()),
                    location=enc(actor.get_actor_location()), rotation=enc(actor.get_actor_rotation()),
                    scale=enc(actor.get_actor_scale3d()))
        report['points'].append(item)
    if isinstance(actor, unreal.GlobalLootSpawnManager):
        report['managers'].append(dict(name=actor.get_name(), **fields(actor,
            ['initialize_on_begin_play', 'use_random_seed', 'spawn_seed', 'ship_upgrade_tree', 'boss_guaranteed_loot_data'])))
    if 'EnemyShip' in cls:
        report['ships'].append(dict(name=actor.get_name(), label=actor.get_actor_label(),
            folder=str(actor.get_folder_path()), location=enc(actor.get_actor_location()),
            **fields(actor, ['chest_spawn_point_chest_settings', 'chest_spawn_point_loot_settings'])))

for path in unreal.EditorAssetLibrary.list_assets('/Game/Blueprints/Item/Data', recursive=True):
    if any(s in path for s in ['ProgressionBalance', 'FixedChestDrops', 'ChestDefinition/', 'RandomGroup/', 'LootTable/']):
        asset = unreal.load_asset(path)
        if not asset:
            continue
        record = {'path': path, 'class': asset.get_class().get_name()}
        if isinstance(asset, unreal.DataTable):
            record['rows'] = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset)
        else:
            record['fields'] = fields(asset, [n for n in dir(asset) if not n.startswith('_')])
            record['fields'] = {k: v for k, v in record['fields'].items() if not isinstance(v, dict) or 'error' not in v}
        report['assets'].append(record)
output_name = 'authored_after.json' if '-LandAuditAfter' in unreal.SystemLibrary.get_command_line() else 'authored_before.json'
(OUT / output_name).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('LAND_RANDOM_AUDIT_COMPLETE points=%d output=%s' % (len(report['points']), OUT))
