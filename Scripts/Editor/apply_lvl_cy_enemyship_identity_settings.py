"""Apply exactly Squad ID, Archetype and Actor Tags by unique Actor Label.

Run in the isolated LvlCYExtraction project with Saved/identity_apply_config.json.
The config selects apply or verify. The script never switches Git branches and
does not require the placed Blueprint class to match the backup Blueprint.
"""
import json
from collections import defaultdict
from pathlib import Path
import unreal


FIELDS = ('squad_id', 'enemy_ship_archetype', 'tags')
RESERVED_PREFIX = 'SWRoomStableId='


def serialize(value):
    if value is None or isinstance(value, (str, bool, int, float)):
        return value
    if isinstance(value, unreal.Name):
        return str(value)
    if isinstance(value, unreal.Actor):
        return {'actor_label': value.get_actor_label(), 'object_name': value.get_name()}
    if isinstance(value, unreal.Object):
        return {'object_path': value.get_path_name()}
    if isinstance(value, (list, tuple, unreal.Array, unreal.Set)):
        return [serialize(item) for item in value]
    if isinstance(value, unreal.StructBase):
        return {'struct_type': type(value).__name__, 'export_text': value.export_text()}
    return str(value)


def transform(actor):
    p, r, s = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
    return {'location': [p.x, p.y, p.z], 'rotation': [r.pitch, r.yaw, r.roll],
            'scale': [s.x, s.y, s.z]}


def inventory(actors, schema):
    result = {}
    for actor in actors:
        row = {'actor_label': actor.get_actor_label(),
               'class_path': actor.get_class().get_path_name(), 'transform': transform(actor)}
        # Record every actor's identity, transform and tags; additionally record
        # all previously audited EnemyShip/Ship and component authoring fields.
        row['tags'] = [str(tag) for tag in actor.get_editor_property('tags')]
        if isinstance(actor, unreal.EnemyShip):
            row['properties'] = {name: serialize(actor.get_editor_property(name))
                                 for name in schema['actor_fields']}
            row['components'] = {}
            for component in actor.get_components_by_class(unreal.ActorComponent):
                names = set()
                for class_name, fields in schema['component_fields'].items():
                    cls = getattr(unreal, class_name, None)
                    if cls and isinstance(component, cls):
                        names.update(fields)
                if names:
                    row['components'][component.get_name()] = {
                        name: serialize(component.get_editor_property(name)) for name in sorted(names)}
        result[actor.get_name()] = row
    return result


def check_only_requested_changes(before, after, targeted_names, allowed_label_changes=None):
    if set(before) != set(after):
        raise RuntimeError('Actor inventory changed')
    violations = []
    for name in before:
        old = dict(before[name])
        new = dict(after[name])
        if allowed_label_changes and name in allowed_label_changes:
            old['actor_label'] = allowed_label_changes[name]
        # ChildActor templates regenerate persistence GUIDs on reconstruction.
        # Keep all authored tags and all top-level actor IDs under strict checks.
        if '_GEN_VARIABLE_' in name and '_CAT_' in name:
            for row in (old, new):
                row['tags'] = [tag for tag in row['tags']
                               if not tag.startswith(RESERVED_PREFIX)]
        if name in targeted_names:
            old.pop('tags', None)
            new.pop('tags', None)
            for row in (old, new):
                if 'properties' in row:
                    row['properties'] = {key: value for key, value in row['properties'].items()
                                         if key not in FIELDS}
        if old != new:
            violations.append(name)
    if violations:
        raise RuntimeError('Unexpected changes outside requested fields: ' + ', '.join(violations))


def main():
    config = json.loads((Path(unreal.Paths.project_saved_dir()) /
                         'identity_apply_config.json').read_text(encoding='utf-8-sig'))
    output = Path(config['output'])
    report = {'mode': config['mode'], 'matching_policy': 'unique_exact_actor_label',
              'requested_fields': list(FIELDS), 'blueprint_class_mismatch_ignored': True,
              'reserved_tag_policy': 'preserve_current_SWRoomStableId',
              'source_manifest': config['manifest'], 'input_map_sha256': config['input_map_sha256'],
              'applied': [], 'missing_labels': [], 'duplicate_labels': {}, 'unsupported': [],
              'success': False}
    manifest = json.loads(Path(config['manifest']).read_text(encoding='utf-8'))
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
    if not world:
        raise RuntimeError('Cannot load isolated map')
    actors = list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    schema = config['schema']
    before = inventory(actors, schema)
    allowed_label_changes = {row['object_name']: row['new_label']
                             for row in config.get('label_corrections', [])}
    report['label_corrections'] = config.get('label_corrections', [])
    if config['mode'] == 'apply':
        for correction in config.get('label_corrections', []):
            matches = [actor for actor in actors if actor.get_name() == correction['object_name']]
            if len(matches) != 1:
                raise RuntimeError('Cannot uniquely resolve label correction')
            actor = matches[0]
            if actor.get_actor_label() not in (correction['old_label'], correction['new_label']):
                raise RuntimeError('Unexpected original actor label')
            actor.modify()
            actor.set_actor_label(correction['new_label'])
    by_label = defaultdict(list)
    for actor in actors:
        by_label[actor.get_actor_label()].append(actor)
    plans = []
    for target in manifest['targets']:
        label = target['actor_label']
        matches = by_label[label]
        if not matches:
            report['missing_labels'].append(label)
            continue
        if len(matches) != 1:
            report['duplicate_labels'][label] = [
                {'object_name': actor.get_name(), 'class_path': actor.get_class().get_path_name(),
                 'transform': transform(actor)} for actor in matches]
            continue
        actor = matches[0]
        props = target['settings_for_transfer']['properties']
        try:
            actual = {name: serialize(actor.get_editor_property(name)) for name in FIELDS}
            path = props['enemy_ship_archetype'].get('asset_path') if props['enemy_ship_archetype'] else None
            archetype = unreal.load_asset(path) if path else None
            if path and not isinstance(archetype, unreal.EnemyShipArchetypeData):
                raise RuntimeError('Missing or invalid Archetype: ' + path)
            tags = list(props['tags'])
            # Room persistence IDs belong to the newly placed actor, not its source.
            tags.extend(tag for tag in actual['tags'] if tag.startswith(RESERVED_PREFIX))
            tags = list(dict.fromkeys(tags))
            desired = {'squad_id': str(props['squad_id']),
                       'enemy_ship_archetype': {'object_path': path} if path else None,
                       'tags': tags}
            plans.append((actor, archetype, desired, actual, target))
        except Exception as exc:
            report['unsupported'].append({'actor_label': label, 'error': str(exc)})
    has_unresolved = bool(report['missing_labels'] or report['duplicate_labels'] or report['unsupported'])
    if has_unresolved and not config.get('allow_partial', False):
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        raise RuntimeError('Preflight failed; map not saved. See ' + str(output))
    if config['mode'] == 'apply':
        for actor, archetype, desired, actual, target in plans:
            actor.modify()
            # Avoid construction reruns that could reset unrelated component settings.
            notify = unreal.PropertyAccessChangeNotifyMode.NEVER
            actor.set_editor_property('squad_id', unreal.Name(desired['squad_id']), notify_mode=notify)
            actor.set_editor_property('enemy_ship_archetype', archetype, notify_mode=notify)
            actor.set_editor_property('tags', [unreal.Name(tag) for tag in desired['tags']], notify_mode=notify)
    after = inventory(list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()), schema)
    check_only_requested_changes(before, after, {actor.get_name() for actor, *_ in plans},
                                 allowed_label_changes)
    if config['mode'] == 'verify':
        baseline = json.loads(Path(config['baseline']).read_text(encoding='utf-8'))
        report['regenerated_child_persistence_id_count'] = sum(
            '_GEN_VARIABLE_' in name and '_CAT_' in name
            and row['tags'] != after[name]['tags']
            for name, row in baseline['before_inventory'].items())
        check_only_requested_changes(baseline['before_inventory'], after,
                                     {actor.get_name() for actor, *_ in plans}, allowed_label_changes)
    for actor, archetype, desired, actual, target in plans:
        observed = {name: serialize(actor.get_editor_property(name)) for name in FIELDS}
        if observed != desired:
            raise RuntimeError('Settings mismatch: ' + actor.get_actor_label())
        report['applied'].append({
            'actor_label': actor.get_actor_label(), 'object_name': actor.get_name(),
            'actual_class_path': actor.get_class().get_path_name(),
            'backup_class_path': target['expected_class_path'],
            'class_differs': actor.get_class().get_path_name() != target['expected_class_path'],
            'before': actual, 'after': observed, 'transform': transform(actor)})
        unreal.log('[LVLCY-APPLY] ' + actor.get_actor_label() + ' ' + desired['squad_id'])
    if config['mode'] == 'apply':
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Level/Lvl_CY'):
            raise RuntimeError('Could not save isolated map')
    report.update(success=True, partial=has_unresolved, matched_actor_count=len(plans),
                  ignored_class_mismatch_count=sum(row['class_differs'] for row in report['applied']),
                  actor_inventory_count=len(after), before_inventory=before, after_inventory=after,
                  original_transforms_and_unrequested_audited_properties_unchanged=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[LVLCY-APPLY] COMPLETE ' + config['mode'] + ' count=' + str(len(plans)))


main()
