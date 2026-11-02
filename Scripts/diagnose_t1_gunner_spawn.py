"""Read-only inspection of saved Lvl_CY boss encounter definitions."""
import json
import unreal


def value(obj, name):
    try:
        result = obj.get_editor_property(name)
        if isinstance(result, unreal.Object):
            return result.get_path_name()
        if isinstance(result, (bool, int, float, str)) or result is None:
            return result
        return str(result)
    except Exception as error:
        return 'UNAVAILABLE: ' + str(error)


unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
report = []
for ship in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if not isinstance(ship, unreal.EnemyShip):
        continue
    encounter = ship.get_component_by_class(unreal.BossEncounterComponent)
    if not encounter:
        continue
    entry = {'actor': ship.get_name(), 'label': ship.get_actor_label(),
             'class': ship.get_class().get_path_name(),
             'encounter': {name: value(encounter, name) for name in [
                 'encounter_enabled', 'boss_class', 'boss_stats_row', 'boss_spawn_point_id',
                 'encounter_trigger', 'required_story_node', 'stop_after_story_node',
                 'trigger_chest_spawn_point_component']}}
    if 'Mid_1' in entry['class'] or entry['encounter']['encounter_enabled']:
        entry['points'] = [{name: value(point, name) for name in [
            'waypoint_id', 'walk_surface_id', 'can_spawn', 'relative_location']}
            for point in ship.get_components_by_class(unreal.DeckWaypointComponent)]
        entry['archetype'] = value(ship, 'enemy_ship_archetype')
        entry['fallback'] = value(ship, 'normal_fallback_archetype')
    report.append(entry)
path = unreal.Paths.project_saved_dir() + 'T1GunnerSpawnDiagnosis.json'
with open(path, 'w', encoding='utf-8') as output:
    json.dump(report, output, ensure_ascii=False, indent=2)
unreal.log('T1_GUNNER_SPAWN_DIAGNOSIS_OK ' + path)
