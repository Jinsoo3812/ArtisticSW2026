import unreal
import json

m = unreal.load_asset('/Game/Blueprints/Water/M_Realistic_Water_Ocean')
e = unreal.MaterialEditingLibrary
r = {'material': m.get_path_name(), 'static': {}, 'scalar': {}}
for n in ['UseFixedZ','UseWaterInfoTexture','UseFixedWaterDepth','Enable Water VS Mapping','UseScalarWaterBodyIndex','Enable Waves','UseBakedSim','UseFixedVelocity']:
    try: r['static'][n] = e.get_material_instance_static_switch_parameter_value(m, n)
    except Exception as ex: r['static'][n] = str(ex)
for n in ['Water Opacity Mask Offset','Refraction','Refraction Far']:
    try: r['scalar'][n] = e.get_material_instance_scalar_parameter_value(m, n)
    except Exception as ex: r['scalar'][n] = str(ex)
if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Level/Lvl_CY'): raise RuntimeError('Load failed')
r['bodies'] = []
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() != 'WaterBodyOcean': continue
    d = {'path': a.get_path_name()}
    c = a.get_component_by_class(unreal.WaterBodyOceanComponent)
    for n in ['height_offset','fixed_water_depth']:
        try: d[n] = str(c.get_editor_property(n))
        except Exception: pass
    s = a.get_component_by_class(unreal.WaterSplineComponent)
    d['spline_world'] = [str(s.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)) for i in range(s.get_number_of_spline_points())]
    w = a.get_editor_property('water_waves')
    b = w.get_editor_property('base_waves_asset')
    d['base_waves_asset'] = b.get_path_name() if b else None
    if b:
        waves = b.get_editor_property('water_waves')
        d['waves'] = str(waves)
        for n in ['gerstner_wave_generator','gerstner_waves']:
            try: d[n] = str(waves.get_editor_property(n))
            except Exception: pass
    r['bodies'].append(d)
with open('C:/Unreal Projects/ArtisticSW2026/Saved/Diagnostics/CY_Shoreline/effective_parameters.json','w',encoding='utf-8') as f: json.dump(r,f,ensure_ascii=False,indent=2)
unreal.log('CY_PARAMETERS_DONE')
