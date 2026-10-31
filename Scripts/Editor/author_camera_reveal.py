"""Author Masked ship reveal variants without changing Nanite/sort flags."""
import hashlib
import json
from pathlib import Path
import traceback
import unreal

ROOT = '/Game/Blueprints/Ship/Materials/CameraReveal'
SAIL = '/Game/Blueprints/Ship/Materials/Sail/'
BLUEPRINTS = ('/Game/Blueprints/Ship/Blueprints/BP_PlayerShip_Kelvin',
              '/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip')
editing = unreal.MaterialEditingLibrary
library = unreal.SWCameraRevealAuthoringLibrary
report = {'assignments': [], 'component_flags': [], 'original_materials': {}}
variants = {}


def variant(source, collection):
    source_path = source.get_path_name()
    if source_path in variants:
        return variants[source_path]
    if source_path.startswith(ROOT + '/'):
        return source
    owned_sail = source_path.startswith(SAIL)
    clip = source.get_editor_property('opacity_mask_clip_value') if isinstance(source, unreal.Material) else None
    if isinstance(source, unreal.MaterialInstanceConstant):
        # Reading the inherited clip value is not exposed by Python. Preserve an
        # explicit original override or the source root's existing clip value.
        current = source
        while isinstance(current, unreal.MaterialInstanceConstant):
            overrides = current.get_editor_property('base_property_overrides')
            if overrides.get_editor_property('override_opacity_mask_clip_value'):
                clip = overrides.get_editor_property('opacity_mask_clip_value')
                break
            current = current.get_editor_property('parent')
        if clip is None:
            clip = current.get_editor_property('opacity_mask_clip_value')
    if owned_sail:
        result = source
    else:
        name = source.get_name() + '_' + hashlib.sha1(source_path.encode()).hexdigest()[:8]
        path = ROOT + '/' + name
        result = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(source_path, path)
        assert result, path
        if source_path.startswith('/Game/'):
            filename = Path(unreal.Paths.project_content_dir()) / (source_path.split('.')[0][6:] + '.uasset')
            report['original_materials'][str(filename)] = hashlib.sha256(filename.read_bytes()).hexdigest()
    if isinstance(result, unreal.MaterialInstanceConstant):
        parent = variant(source.get_editor_property('parent'), collection)
        editing.set_material_instance_parent(result, parent)
        assert library.configure_instance(result, clip)
        editing.update_material_instance(result)
    elif isinstance(result, unreal.Material):
        assert library.add_masked_reveal(result, collection), result.get_path_name()
    else:
        raise RuntimeError('Unsupported source: ' + source_path)
    assert unreal.EditorAssetLibrary.save_loaded_asset(result, False)
    variants[source_path] = result
    return result


def author():
    collection = library.create_parameters()
    assert collection
    assert unreal.EditorAssetLibrary.save_loaded_asset(collection, False)
    sail = unreal.load_asset(SAIL + 'MI_PlayerSail')
    tint_before = editing.get_material_instance_vector_parameter_value(sail, 'SailTint')
    strength_before = editing.get_material_instance_scalar_parameter_value(sail, 'TintStrength')
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    functions = unreal.SubobjectDataBlueprintFunctionLibrary
    for path in BLUEPRINTS:
        bp = unreal.load_asset(path)
        handled = set()
        for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
            obj = functions.get_object_for_blueprint(subsystem.k2_find_subobject_data_from_handle(handle), bp)
            if not isinstance(obj, unreal.StaticMeshComponent) or obj.get_path_name() in handled:
                continue
            handled.add(obj.get_path_name())
            mesh = obj.get_editor_property('static_mesh')
            if not mesh or not obj.get_editor_property('visible') or obj.get_editor_property('hidden_in_game'):
                continue
            flags = (obj.get_editor_property('disallow_nanite'), obj.get_editor_property('sort_triangles'))
            for index in range(obj.get_num_materials()):
                source = obj.get_material(index)
                if not source or 'Window' in source.get_name():
                    continue
                result = variant(source, collection)
                obj.modify()
                obj.set_material(index, result)
                report['assignments'].append({'bp': path, 'component': obj.get_name(), 'slot': index,
                                             'original': source.get_path_name(), 'variant': result.get_path_name()})
            after = (obj.get_editor_property('disallow_nanite'), obj.get_editor_property('sort_triangles'))
            assert flags == after
            report['component_flags'].append({'component': obj.get_path_name(), 'disallow_nanite': after[0],
                'sort_triangles': after[1], 'mesh_nanite_enabled': mesh.get_editor_property('nanite_settings').get_editor_property('enabled')})
        bp.modify()
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    controller = unreal.load_asset('/Game/Blueprints/Player/BP_BasePlayerController')
    unreal.BlueprintEditorLibrary.compile_blueprint(controller)
    assert unreal.EditorAssetLibrary.save_loaded_asset(controller, False)
    materials = []
    for folder in (ROOT, SAIL.rstrip('/')):
        for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
            material = unreal.load_asset(path)
            if isinstance(material, unreal.MaterialInterface):
                materials.append(material)
    report['compilation'] = library.finish_materials(materials)
    assert report['compilation'].startswith('SUCCESS:'), report['compilation']
    tint_after = editing.get_material_instance_vector_parameter_value(sail, 'SailTint')
    strength_after = editing.get_material_instance_scalar_parameter_value(sail, 'TintStrength')
    assert tint_before == tint_after and strength_before == strength_after
    report['sail_tint'] = str(tint_after)
    report['sail_strength'] = strength_after
    for filename, before in report['original_materials'].items():
        assert hashlib.sha256(Path(filename).read_bytes()).hexdigest() == before, filename
    report['success'] = True


try:
    author()
except Exception:
    report['error'] = traceback.format_exc()
    unreal.log_error(report['error'])
finally:
    (Path(unreal.Paths.project_saved_dir()) / 'CameraRevealAuthoring.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
