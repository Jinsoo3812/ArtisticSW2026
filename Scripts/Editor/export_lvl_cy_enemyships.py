"""Read EnemyShip instance settings from an isolated audit project; never save assets.

The audit project's Saved/extraction_config.json supplies output and provenance.
Used to preserve level instance authoring across a binary-map merge.
"""
import json
import math
from pathlib import Path
import unreal


def encode(value, depth=0):
    if depth > 8:
        return {"type": type(value).__name__, "text": str(value)}
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        return value if math.isfinite(value) else str(value)
    if isinstance(value, unreal.Name):
        return str(value)
    if isinstance(value, unreal.Actor):
        return {"actor_label": value.get_actor_label(), "object_name": value.get_name(),
                "class_path": value.get_class().get_path_name()}
    if isinstance(value, unreal.ActorComponent):
        owner = value.get_owner()
        return {"component_name": value.get_name(),
                "owner_label": owner.get_actor_label() if owner else None,
                "class_path": value.get_class().get_path_name()}
    if isinstance(value, unreal.Object):
        return {"asset_path": value.get_path_name()}
    if isinstance(value, (list, tuple, unreal.Array, unreal.Set)):
        return [encode(v, depth + 1) for v in value]
    if isinstance(value, (dict, unreal.Map)):
        return {str(k): encode(v, depth + 1) for k, v in value.items()}
    if isinstance(value, unreal.StructBase):
        fields = {}
        for name in dir(value):
            if name.startswith('_'):
                continue
            try:
                member = value.get_editor_property(name)
                fields[name] = encode(member, depth + 1)
            except Exception:
                pass
        return {"struct_type": type(value).__name__, "fields": fields,
                "export_text": value.export_text()}
    return {"type": type(value).__name__, "text": str(value)}


def read_fields(obj, names):
    fields, errors = {}, {}
    for name in sorted(set(names)):
        try:
            fields[name] = encode(obj.get_editor_property(name))
        except Exception as exc:
            errors[name] = str(exc)
    return fields, errors


def main():
    config_path = Path(unreal.Paths.project_saved_dir()) / 'extraction_config.json'
    config = json.loads(config_path.read_text(encoding='utf-8-sig'))
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
    if not world:
        raise RuntimeError('Failed to load isolated Lvl_CY')
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(subsystem.get_all_level_actors())
    report = dict(config['provenance'])
    report.update(schema_version=1, map_path='/Game/Level/Lvl_CY',
                  total_actor_count=len(actors), enemyships=[],
                  other_actor_index=[], read_only=True)
    for actor in actors:
        identity = {'actor_label': actor.get_actor_label(), 'object_name': actor.get_name(),
                    'class_path': actor.get_class().get_path_name()}
        if not isinstance(actor, unreal.EnemyShip):
            report['other_actor_index'].append(identity)
            continue
        blueprint_names = [name for name in dir(actor)
                           if not name.startswith('_') and name not in dir(unreal.EnemyShip)]
        fields, errors = read_fields(actor, config['actor_fields'])
        blueprint_fields, _ = read_fields(actor, blueprint_names)
        row = dict(identity)
        row.update(properties=fields, blueprint_properties=blueprint_fields,
                   unreadable_properties=errors,
                   blueprint_path=actor.get_class().get_path_name().removesuffix('_C'),
                   location=encode(actor.get_actor_location()),
                   rotation=encode(actor.get_actor_rotation()),
                   scale=encode(actor.get_actor_scale3d()), components={})
        try:
            row['actor_guid'] = encode(actor.get_editor_property('actor_guid'))
        except Exception:
            row['actor_guid'] = None
        for component in actor.get_components_by_class(unreal.ActorComponent):
            names = []
            for class_name, field_names in config['component_fields'].items():
                component_class = getattr(unreal, class_name, None)
                if component_class and isinstance(component, component_class):
                    names.extend(field_names)
            if not names:
                continue
            component_fields, component_errors = read_fields(component, names)
            row['components'][component.get_name()] = {
                'class_path': component.get_class().get_path_name(),
                'properties': component_fields, 'unreadable_properties': component_errors}
        report['enemyships'].append(row)
        unreal.log('[LVLCY-EXPORT] ' + row['actor_label'] + ' ' + str(fields.get('squad_id')))
    report['enemyships'].sort(key=lambda row: (row['actor_label'], row['object_name']))
    report['enemyship_count'] = len(report['enemyships'])
    output = Path(config['output'])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('[LVLCY-EXPORT] COMPLETE ' + str(output) + ' ships=' + str(report['enemyship_count']))


main()
