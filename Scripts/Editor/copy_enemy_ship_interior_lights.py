import json
import shutil
from pathlib import Path
import unreal

source = unreal.load_asset('/Game/Blueprints/Ship/Blueprints/BP_PlayerShip_Kelvin')
target = unreal.load_asset('/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip')
subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
functions = unreal.SubobjectDataBlueprintFunctionLibrary

def parts(bp):
    result = {}
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = subsystem.k2_find_subobject_data_from_handle(handle)
        obj = functions.get_object_for_blueprint(data, bp)
        if obj:
            result[obj.get_name().removesuffix('_GEN_VARIABLE')] = (handle, obj)
    return result

src = parts(source)
dst = parts(target)
names = ('PointLight', 'PointLight2')
assert all(isinstance(src[name][1], unreal.PointLightComponent) for name in names)
project = Path(unreal.Paths.project_dir())
backup = project / 'Saved/ShipInteriorLightsBackup/BP_EnemyShip.uasset'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    shutil.copy2(project / 'Content/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.uasset', backup)
missing = [src[name][0] for name in names if name not in dst]
verify_only = '-VerifyShipInterior' in unreal.SystemLibrary.get_command_line()
assert not verify_only or not missing, 'Saved light components are missing'
if missing:
    assert subsystem.can_copy_subobjects(missing)
    subsystem.copy_subobjects(missing, source)
    pasted = subsystem.paste_subobjects(dst['BuoyancyRoot'][0], [dst['BuoyancyRoot'][0]], target)
    assert len(pasted) == len(missing), str(pasted)
    unreal.BlueprintEditorLibrary.compile_blueprint(target)
    dst = parts(target)

keys = ('relative_location', 'relative_rotation', 'relative_scale3d', 'intensity',
        'attenuation_radius', 'light_color', 'intensity_units', 'cast_shadows',
        'mobility', 'visible', 'hidden_in_game', 'source_radius', 'soft_source_radius',
        'source_length', 'use_temperature', 'temperature', 'indirect_lighting_intensity',
        'volumetric_scattering_intensity', 'use_inverse_squared_falloff',
        'light_falloff_exponent', 'max_draw_distance', 'max_distance_fade_range')
report = {}
for name in names:
    a, b = src[name][1], dst[name][1]
    report[name] = {}
    data = subsystem.k2_find_subobject_data_from_handle(dst[name][0])
    parent_handle = functions.get_parent_handle(data)
    parent_data = subsystem.k2_find_subobject_data_from_handle(parent_handle)
    parent = functions.get_object_for_blueprint(parent_data, target)
    assert parent and parent.get_name() == 'BuoyancyRoot', str(parent)
    report[name]['parent'] = parent.get_name()
    for key in keys:
        av, bv = a.get_editor_property(key), b.get_editor_property(key)
        if key == 'light_color':
            av = (av.r, av.g, av.b, av.a)
            bv = (bv.r, bv.g, bv.b, bv.a)
        assert av == bv, (name, key, av, bv)
        report[name][key] = str(bv)
    assert b.get_editor_property('visible') and not b.get_editor_property('hidden_in_game')
if not verify_only:
    assert unreal.EditorAssetLibrary.save_loaded_asset(target, False)
lights = [name for name, (_, obj) in dst.items() if isinstance(obj, unreal.LocalLightComponent)]
assert len(lights) == 3, lights
report['lights'] = lights
defaults = unreal.get_default_object(target.generated_class())
report['culling_enable_distance'] = defaults.get_editor_property('cabin_light_enable_distance')
report['culling_disable_distance'] = defaults.get_editor_property('cabin_light_disable_distance')
(project / 'Saved/ShipInteriorLightsReport.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('SHIP_INTERIOR_LIGHTS_OK ' + json.dumps(report))
