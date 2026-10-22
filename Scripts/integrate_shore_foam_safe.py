import json, os, traceback
import unreal

PATH = '/Game/Blueprints/Water/M_Realistic_Water'
OUT = 'C:/Unreal Projects/ArtisticSW2026/Saved/Diagnostics/ShoreFoam_Implementation_20261008'
H = unreal.RealisticWaterMaterialPipelineLibrary
E = unreal.MaterialEditingLibrary


def dump(name, data):
    with open(os.path.join(OUT, name + '.json'), 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)


def prop(obj, key):
    try:
        return obj.get_editor_property(key)
    except Exception:
        return None


def snapshot(material):
    result = {}
    for node in H.get_material_expressions(material):
        names = list(E.get_material_expression_input_names(node))
        edges = {}
        for index in range(32):
            source = H.get_connected_input_expression(node, index)
            if source:
                edges[str(index)] = [source.get_name(), str(E.get_input_node_output_name_for_material_expression(node, source))]
        record = {'class': node.get_class().get_name(), 'inputs': [str(n) for n in names], 'edges': edges}
        for key in ['desc', 'parameter_name', 'default_value', 'code', 'output_type', 'texture', 'material_function', 'material_expression_editor_x', 'material_expression_editor_y']:
            value = prop(node, key)
            if value is not None:
                record[key] = value.get_path_name() if isinstance(value, unreal.Object) else str(value)
        result[node.get_name()] = record
    return result


def stats(material):
    values = E.get_statistics(material)
    result = {key: int(values.get_editor_property(key)) for key in [
        'num_vertex_shader_instructions', 'num_pixel_shader_instructions', 'num_samplers',
        'num_vertex_texture_samples', 'num_pixel_texture_samples']}
    if result['num_pixel_shader_instructions'] <= 0:
        raise RuntimeError('No compiled pixel shader statistics: ' + str(result))
    return result


def connect(source, output, target, pin):
    if not E.connect_material_expressions(source, output, target, pin):
        raise RuntimeError('Failed connection: ' + source.get_name() + '.' + output + ' -> ' + target.get_name() + '.' + pin)


def create(material, cls, desc, x, y):
    node = E.create_material_expression(material, cls, x, y)
    node.set_editor_property('desc', desc)
    return node


def main():
    material = unreal.load_asset(PATH)
    if not material:
        raise RuntimeError('Material not found')
    nodes = {n.get_name(): n for n in H.get_material_expressions(material)}
    if any(str(prop(n, 'parameter_name')) == 'Enable Shore Foam' for n in nodes.values()):
        raise RuntimeError('Already integrated; refusing duplicate mutation')
    before = snapshot(material)
    dump('before_graph', before)
    baseline_stats = stats(material)
    dump('baseline_stats', baseline_stats)
    attributes = nodes['MaterialExpressionSetMaterialAttributes_2']
    base = nodes['MaterialExpressionBreakMaterialAttributes_1']
    mask = nodes['MaterialExpressionCustom_1']
    if H.get_connected_input_expression(attributes, 2) or H.get_connected_input_expression(attributes, 7):
        raise RuntimeError('Base Color/Opacity are now connected; refusing to overwrite')
    if list(E.get_material_expression_input_names(attributes))[2] != 'Opacity Override' or list(E.get_material_expression_input_names(attributes))[7] != 'Base Color':
        raise RuntimeError('Attribute pins do not match inspected schema')
    if 'CalculateGodotFoam' not in str(prop(mask, 'code')):
        raise RuntimeError('Shore source changed')
    fallback = float(prop(nodes['MaterialExpressionSceneDepthWithoutWater_0'], 'fallback_depth'))
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mask.set_editor_property('code', '''return SW_CalculateShoreFoamSafe(
    SceneDepth, PixelDepth, BaseUV.xy, TimeDir.xy,
    FoamScale, FoamDepthThreshold, FoamTexture, FoamTextureSampler,
    %s);''' % format(fallback, '.1f'))
    mask.set_editor_property('description', 'SW Shore Foam safe contact mask; preserve original noise shape')

    strength = create(material, unreal.MaterialExpressionScalarParameter, 'SW Shore Foam coverage strength', 6500, -1950)
    strength.set_editor_property('parameter_name', 'ShoreFoamStrength')
    strength.set_editor_property('default_value', 1.0)
    strength.set_editor_property('group', 'Shore Foam')
    face = create(material, unreal.MaterialExpressionTwoSidedSign, 'SW Shore Foam upper surface only', 6500, -1800)
    surface = create(material, unreal.MaterialExpressionCustom, 'SW Shore Foam Surface RGBA (color + coverage)', 6900, -2300)
    if not H.configure_float4_custom_expression(surface,
            ['OriginalBaseColor', 'OriginalOpacity', 'FoamColor', 'Alpha', 'Strength', 'FaceSign'],
            'return SW_ComposeShoreFoamSurface(OriginalBaseColor, OriginalOpacity, FoamColor, Alpha, Strength, FaceSign);',
            'SW Shore Foam Surface RGBA (color + coverage)'):
        raise RuntimeError('Could not configure Shore surface')
    surface.set_editor_property('include_file_paths', ['/Project/GodotFoam.ush'])
    connect(base, 'BaseColor', surface, 'OriginalBaseColor')
    connect(base, 'Opacity', surface, 'OriginalOpacity')
    connect(nodes['MaterialExpressionConstant3Vector_2'], '', surface, 'FoamColor')
    connect(mask, '', surface, 'Alpha')
    connect(strength, '', surface, 'Strength')
    connect(face, '', surface, 'FaceSign')

    rgb = create(material, unreal.MaterialExpressionComponentMask, 'SW Shore Foam composed Base Color', 7400, -2300)
    alpha = create(material, unreal.MaterialExpressionComponentMask, 'SW Shore Foam composed Opacity', 7400, -2050)
    for component in ['r', 'g', 'b', 'a']:
        rgb.set_editor_property(component, component != 'a')
        alpha.set_editor_property(component, component == 'a')
    connect(surface, '', rgb, '')
    connect(surface, '', alpha, '')

    switches = []
    for label, true_source, false_output, y in [('Base Color', rgb, 'BaseColor', -2300), ('Opacity', alpha, 'Opacity', -2050)]:
        switch = create(material, unreal.MaterialExpressionStaticSwitchParameter, 'SW Enable Shore Foam ' + label, 7800, y)
        switch.set_editor_property('parameter_name', 'Enable Shore Foam')
        switch.set_editor_property('default_value', True)
        switch.set_editor_property('group', 'Shore Foam')
        connect(true_source, '', switch, 'True')
        connect(base, false_output, switch, 'False')
        switches.append(switch)
    connect(switches[0], '', attributes, 'Base Color')
    connect(switches[1], '', attributes, 'Opacity Override')
    connect(base, 'BaseColor', nodes['MaterialExpressionLinearInterpolate_0'], 'A')

    after = snapshot(material)
    expected_edges = {('MaterialExpressionSetMaterialAttributes_2', '2'), ('MaterialExpressionSetMaterialAttributes_2', '7'), ('MaterialExpressionLinearInterpolate_0', '0')}
    changed_edges = set()
    for name, record in before.items():
        if name not in after:
            raise RuntimeError('Original node removed: ' + name)
        other = after[name]
        for pin in set(record['edges']) | set(other['edges']):
            if record['edges'].get(pin) != other['edges'].get(pin):
                changed_edges.add((name, pin))
        for key, value in record.items():
            if key == 'edges' or (name == mask.get_name() and key in ['code', 'output_type', 'desc']):
                continue
            if other.get(key) != value:
                raise RuntimeError('Unexpected node property change: ' + name + '.' + key)
    if changed_edges != expected_edges:
        raise RuntimeError('Unexpected connection changes: ' + str(changed_edges))
    if E.get_material_property_input_node(material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES).get_name() != 'MaterialExpressionSetMaterialAttributes_3':
        raise RuntimeError('Final Vortex attribute chain changed')
    dump('after_graph', after)
    E.recompile_material(material)
    active_stats = stats(material)
    dump('active_stats', active_stats)

    off = unreal.new_object(unreal.MaterialInstanceConstant, name='SWShoreFoamOffValidation')
    E.set_material_instance_parent(off, material)
    E.set_material_instance_static_switch_parameter_value(off, 'Enable Shore Foam', False)
    if E.get_material_instance_static_switch_parameter_value(off, 'Enable Shore Foam'):
        raise RuntimeError('Could not set transient disabled permutation')
    off_stats = stats(off)
    if off_stats['num_pixel_texture_samples'] != baseline_stats['num_pixel_texture_samples'] or off_stats['num_vertex_texture_samples'] != baseline_stats['num_vertex_texture_samples']:
        raise RuntimeError('Disabled Shore sampling differs from baseline')
    dump('verification', {
        'baseline': baseline_stats, 'active': active_stats, 'disabled': off_stats,
        'preserved_original_nodes': len(before), 'added_nodes': len(after) - len(before),
        'changed_existing_edges': sorted([list(x) for x in changed_edges]),
        'default_enabled': E.get_material_default_static_switch_parameter_value(material, 'Enable Shore Foam'),
        'strength': E.get_material_default_scalar_parameter_value(material, 'ShoreFoamStrength'),
        'final_attributes': E.get_material_property_input_node(material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES).get_name(),
        'compiled_active_and_disabled': True, 'saved': False})
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError('Saving material failed')
    with open(os.path.join(OUT, 'verification.json'), encoding='utf-8') as stream:
        verification = json.load(stream)
    verification['saved'] = True
    dump('verification', verification)
    with open(os.path.join(OUT, 'success.txt'), 'w') as stream:
        stream.write('Saved master; verified original edges and both shader permutations.\n')
    unreal.log('SHORE_FOAM_INTEGRATION=PASS')


try:
    main()
except Exception:
    dump('failure', {'error': traceback.format_exc()})
    unreal.log_error(traceback.format_exc())
    raise




