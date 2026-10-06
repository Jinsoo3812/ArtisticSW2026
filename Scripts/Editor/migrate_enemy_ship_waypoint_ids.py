"""Migrate the authored BP_EnemyShip deck anchors to lower 0/1, upper 10/11/12.

Run in Unreal Editor Python after closing other editor instances. New optional spawn
anchors overlap existing valid points initially and remain disabled until placed.
"""

import json
import os
import shutil

import unreal


BP_PATH = '/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip'
MAP_PATH = '/Game/Level/Test_Level'
BACKUP = os.path.join(unreal.Paths.project_saved_dir(), 'DeckWaypointIdMigrationBackup')
os.makedirs(BACKUP, exist_ok=True)


def backup(relative):
    source = os.path.join(unreal.Paths.project_dir(), 'Content', relative)
    target = os.path.join(BACKUP, relative)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if not os.path.exists(target):
        shutil.copy2(source, target)


backup('Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.uasset')
backup('Level/Test_Level.umap')
bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
assert bp, BP_PATH
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary


def templates():
    result = {}
    for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
        obj = lib.get_object_for_blueprint(lib.get_data(handle), bp)
        if obj:
            result[obj.get_name().removesuffix('_GEN_VARIABLE')] = (handle, obj)
    return result


items = templates()
for name in ('L_MeleeEnemySpawnPoint_1', 'U_MeleeEnemySpawnPoint_1',
             'BossSpawnPoint', 'DeckMesh_Complex', 'DeckWalkAreaComponent',
             'DeckEnemySpawnerComponent', 'BossEncounterComponent'):
    assert name in items, f'Missing Blueprint component {name}'

expected = {'L_MeleeEnemySpawnPoint_1': (0, 0),
            'U_MeleeEnemySpawnPoint_1': (2001, 10),
            'BossSpawnPoint': (12345, 12)}
for name, (old_id, new_id) in expected.items():
    point = items[name][1]
    assert point.get_editor_property('waypoint_id') in (old_id, new_id), name
    point.set_editor_property('waypoint_id', new_id)
    point.set_editor_property('walk_surface_id', unreal.Name('LowerDeck' if new_id < 10 else 'UpperDeck'))


def add_optional_anchor(name, point_id, surface, copy_from):
    existing = templates().get(name)
    if existing:
        point = existing[1]
        assert point.get_editor_property('waypoint_id') == point_id, name
    else:
        params = unreal.AddNewSubobjectParams(
            parent_handle=items['DeckMesh_Complex'][0],
            new_class=unreal.DeckWaypointComponent,
            blueprint_context=bp)
        handle, failure = sub.add_new_subobject(params)
        assert not str(failure), f'Add {name}: {failure}'
        assert sub.rename_subobject(handle, name), f'Rename {name}'
        point = lib.get_object_for_blueprint(lib.get_data(handle), bp)
        assert point, name
        point.set_editor_property('relative_location',
                                  items[copy_from][1].get_editor_property('relative_location'))
    point.set_editor_property('waypoint_id', point_id)
    point.set_editor_property('walk_surface_id', unreal.Name(surface))
    point.set_editor_property('can_spawn', False)


add_optional_anchor('L_EnemySpawnPoint_1', 1, 'LowerDeck', 'L_MeleeEnemySpawnPoint_1')
add_optional_anchor('U_EnemySpawnPoint_11', 11, 'UpperDeck', 'U_MeleeEnemySpawnPoint_1')

area = items['DeckWalkAreaComponent'][1]
surfaces = list(area.get_editor_property('surfaces'))
assert {str(s.get_editor_property('surface_id')) for s in surfaces} == {'LowerDeck', 'UpperDeck'}
for surface in surfaces:
    if str(surface.get_editor_property('surface_id')) == 'UpperDeck':
        assert surface.get_editor_property('height_reference_point_id') in (2001, 10)
        surface.set_editor_property('height_reference_point_id', 10)
area.set_editor_property('surfaces', surfaces)

spawner = items['DeckEnemySpawnerComponent'][1]
plan = list(spawner.get_editor_property('spawn_plan'))
for slot in plan:
    if slot.get_editor_property('spawn_point_id') == 2001:
        slot.set_editor_property('spawn_point_id', 10)
spawner.set_editor_property('spawn_plan', plan)
encounter = items['BossEncounterComponent'][1]
assert encounter.get_editor_property('boss_spawn_point_id') in (12345, 12)
encounter.set_editor_property('boss_spawn_point_id', 12)

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)

unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
results = []
for ship in actors.get_all_level_actors():
    if not isinstance(ship, unreal.EnemyShip) or ship.get_class() != bp.generated_class():
        continue
    points = {p.get_name(): p for p in ship.get_components_by_class(unreal.DeckWaypointComponent)}
    for name, (_, new_id) in expected.items():
        assert name in points, f'{ship.get_actor_label()} missing {name}'
        points[name].set_editor_property('waypoint_id', new_id)
        points[name].set_editor_property('walk_surface_id',
                                         unreal.Name('LowerDeck' if new_id < 10 else 'UpperDeck'))
    for name, point_id, surface in (('L_EnemySpawnPoint_1', 1, 'LowerDeck'),
                                    ('U_EnemySpawnPoint_11', 11, 'UpperDeck')):
        assert name in points, f'{ship.get_actor_label()} missing {name}'
        points[name].set_editor_property('waypoint_id', point_id)
        points[name].set_editor_property('walk_surface_id', unreal.Name(surface))
        points[name].set_editor_property('can_spawn', False)
    ship_area = ship.get_component_by_class(unreal.DeckWalkAreaComponent)
    ship_surfaces = list(ship_area.get_editor_property('surfaces'))
    assert next(s for s in ship_surfaces if str(s.get_editor_property('surface_id')) == 'UpperDeck').get_editor_property('height_reference_point_id') == 10
    ship_spawner = ship.get_component_by_class(unreal.DeckEnemySpawnerComponent)
    ship_plan = list(ship_spawner.get_editor_property('spawn_plan'))
    assert all(slot.get_editor_property('spawn_point_id') != 2001 for slot in ship_plan)
    ship.get_component_by_class(unreal.BossEncounterComponent).set_editor_property('boss_spawn_point_id', 12)
    unreal.SystemLibrary.execute_console_command(ship, 'Editor.AsyncStaticMeshCompilationFinishAll')
    ship_area.rebuild()
    ids = sorted(p.get_editor_property('waypoint_id') for p in points.values())
    lower = ship_area.get_surface_node_count('LowerDeck')
    upper = ship_area.get_surface_node_count('UpperDeck')
    results.append({'ship': ship.get_actor_label(), 'ids': ids, 'ready': ship_area.is_ready(),
                    'lower_nodes': lower, 'upper_nodes': upper,
                    'spawn_ids': [slot.get_editor_property('spawn_point_id') for slot in ship_plan],
                    'boss_id': ship.get_component_by_class(unreal.BossEncounterComponent).get_editor_property('boss_spawn_point_id')})
    assert ids == [0, 1, 10, 11, 12] and ship_area.is_ready() and lower > 0 and upper > 0, results[-1]
    assert results[-1]['spawn_ids'] == [0, 10] and results[-1]['boss_id'] == 12, results[-1]

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
unreal.log('DECK_WAYPOINT_MIGRATION ' + json.dumps(results))
