import unreal
import json
import os

OUT = 'C:/Unreal Projects/ArtisticSW2026/Saved/Diagnostics/CY_Shoreline'
os.makedirs(OUT, exist_ok=True)
def value(v):
    if isinstance(v, unreal.Object): return v.get_path_name()
    if isinstance(v, (bool, int, float, str)) or v is None: return v
    try: return [value(x) for x in v]
    except Exception: return str(v)
def props(o, names):
    d = {'path': o.get_path_name(), 'class': o.get_class().get_name()}
    for n in names:
        try: d[n] = value(o.get_editor_property(n))
        except Exception: pass
    return d
def save(name, d):
    with open(os.path.join(OUT, name+'.json'), 'w', encoding='utf-8') as f: json.dump(d, f, ensure_ascii=False, indent=2)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level('/Game/Level/Lvl_CY'): raise RuntimeError('Lvl_CY load failed')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
records = []
materials = []
names = ['water_material','water_info_material','underwater_post_process_material','water_waves','water_heightmap_settings','water_curve_settings','water_body_index','height_offset','ocean_extents','collision_extents','center_on_water_zone','affects_landscape','generate_collisions','water_zone_override','water_mesh_override','target_wave_mask_depth','max_wave_height_offset','water_material_instance','shape_dilation','water_body_render_data','zone_extent','render_target_resolution','capture_z_offset','enable_local_only_tessellation','local_tessellation_extent','water_info_texture','half_precision_texture','tile_size','extent_in_tiles','tessellation_factor','lod_scale','far_distance_material','far_distance_mesh_extent','use_gpu_quad_tree','landscape_material','landscape_hole_material','use_nanite','enable_nanite','nanite_skirt_enabled','nanite_skirt_depth','water_brush_effects','water_heightmap_settings']
for a in actors:
    cls = a.get_class().get_name()
    if not any(s in cls for s in ['Water','Landscape']): continue
    r = props(a, names)
    r['label'] = a.get_actor_label()
    r['location'] = str(a.get_actor_location())
    r['rotation'] = str(a.get_actor_rotation())
    r['scale'] = str(a.get_actor_scale3d())
    r['components'] = []
    for c in a.get_components_by_class(unreal.ActorComponent):
        if any(s in c.get_class().get_name() for s in ['Water','Landscape']):
            r['components'].append(props(c, names))
            for p in ['water_material','far_distance_material']:
                try:
                    m = c.get_editor_property(p)
                    if m: materials.append(m)
                except Exception: pass
            try:
                w = c.get_editor_property('water_waves')
                if w:
                    wr = props(w, ['water_waves','gerstner_waves','gerstner_wave_generator'])
                    for p in ['water_waves','gerstner_wave_generator']:
                        try:
                            sub = w.get_editor_property(p)
                            if sub: wr[p] = props(sub, ['gerstner_waves','gerstner_wave_generator','num_waves','min_wavelength','max_wavelength','min_amplitude','max_amplitude','steepness','seed'])
                        except Exception: pass
                    r['waves_detail'] = wr
            except Exception: pass
    records.append(r)
save('level', {'loaded': '/Game/Level/Lvl_CY', 'actor_count':len(actors),'actors':records})
helper = unreal.RealisticWaterMaterialPipelineLibrary
visited = set()
def graph(obj):
    path = obj.get_path_name()
    if path in visited: return
    visited.add(path)
    r = props(obj, ['parent','blend_mode','shading_model','two_sided','use_material_attributes','refraction_method','scalar_parameter_values','vector_parameter_values','texture_parameter_values','static_parameters'])
    if isinstance(obj, unreal.MaterialInstanceConstant):
        try: graph(obj.get_editor_property('parent'))
        except Exception: pass
    else:
        isfunc = isinstance(obj, unreal.MaterialFunction)
        es = helper.get_material_function_expressions(obj) if isfunc else helper.get_material_expressions(obj)
        r['expressions'] = []
        for e in es:
            er = props(e, ['desc','parameter_name','default_value','material_function','code','inputs','outputs','input_name','output_name','attribute_set_types','const_a','const_b','constant','value','a','b','input','function_inputs','function_outputs'])
            er['connections'] = {}
            for i in range(32):
                try:
                    src = helper.get_connected_input_expression(e,i)
                    if src: er['connections'][str(i)] = src.get_path_name()
                except Exception: pass
            r['expressions'].append(er)
            try:
                func = e.get_editor_property('material_function')
                if func: graph(func)
            except Exception: pass
        if not isfunc:
            r['property_inputs'] = {}
            for n in ['MP_WORLD_POSITION_OFFSET','MP_MATERIAL_ATTRIBUTES','MP_NORMAL','MP_REFRACTION','MP_PIXEL_DEPTH_OFFSET','MP_OPACITY_MASK']:
                try:
                    node = unreal.MaterialEditingLibrary.get_material_property_input_node(obj,getattr(unreal.MaterialProperty,n))
                    r['property_inputs'][n] = value(node)
                except Exception: pass
    save(obj.get_name(), r)
for m in materials:
    try: graph(m)
    except Exception as e: unreal.log_warning(str(e))
unreal.log('CY_SHORELINE_DONE '+OUT)
