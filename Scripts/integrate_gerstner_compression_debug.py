"""Install the synchronized Gerstner crest/compression mask."""
import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
GROUP = "Gerstner Crest Foam"
INDEX_DESC = "SW Gerstner Compression Water Body Index"
TIME_DESC = "SW Gerstner Compression Time"
CUSTOM_DESC = "SW Gerstner Crest Compression Mask"
WORLD_DESC = "SW Gerstner Compression World Position (excluding WPO)"

CUSTOM_CODE = r"""return SW_ComputeGerstnerCrestFoamMask(
    WaterBodyIndex,
    WaterTime,
    MakeLWCVector2(LWCLWCWorldPosition.Tile.xy, LWCLWCWorldPosition.Offset.xy),
    JacobianStart,
    JacobianFull,
    CrestWidth,
    CrestSharpness,
    CrestMinWaveAmplitude,
    CompressionInfluence);"""


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=""):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def find_by_name(expressions, wanted):
    return next((e for e in expressions if short_name(e) == wanted), None)


def find_by_desc(expressions, wanted):
    for expression in expressions:
        if (str(prop(expression, "description")) == wanted
                or str(prop(expression, "desc")) == wanted):
            return expression
    return None


def find_parameter(expressions, wanted):
    return next((e for e in expressions
                 if str(prop(e, "parameter_name")) == wanted), None)


def find_function(expressions, function_path):
    for expression in expressions:
        function = prop(expression, "material_function", None)
        if function is not None:
            try:
                if function.get_path_name() == function_path:
                    return expression
            except Exception:
                if function_path in str(function):
                    return expression
    return None


def connect(editing, source, output_name, target, input_name):
    if not editing.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError("Connection failed: {}.{} -> {}.{}".format(
            short_name(source), output_name, short_name(target), input_name))


def scalar_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", default)
    expression.set_editor_property("group", GROUP)
    return expression


def main():
    material = unreal.load_asset(ASSET_PATH)
    if material is None:
        raise RuntimeError("Could not load " + ASSET_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))

    water_index = find_by_desc(expressions, INDEX_DESC)
    if water_index is None:
        water_index = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 7800, 3900)
        expressions.append(water_index)
    water_index.set_editor_property(
        "code", "return GetWaterWaveParamIndex(Parameters);")
    water_index.set_editor_property("description", INDEX_DESC)
    water_index.set_editor_property(
        "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)

    world_position = find_by_desc(expressions, WORLD_DESC)
    if world_position is None:
        world_position = editing.create_material_expression(
            material, unreal.MaterialExpressionWorldPosition, 7800, 4060)
        world_position.set_editor_property("desc", WORLD_DESC)
        try:
            world_position.set_editor_property(
                "world_position_shader_offset",
                unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        except Exception:
            # UE Python enum spelling differs across minor versions. The node is
            # still valid; validation additionally checks the debug is camera independent.
            pass
        expressions.append(world_position)

    water_time = find_function(
        expressions, "/Water/Materials/Functions/GetWaterTime.GetWaterTime")
    if water_time is None:
        raise RuntimeError("ComputeGerstnerWaves WaterTime source is missing")

    # Remove the former independent material clock after reconnecting. The
    # actual Gerstner displacement uses the Water subsystem's GetWaterTime.
    time_node = find_by_desc(expressions, TIME_DESC)

    jacobian_start = scalar_parameter(
        material, editing, expressions, "SW Gerstner Jacobian Start",
        0.95, 7800, 4380)
    jacobian_full = scalar_parameter(
        material, editing, expressions, "SW Gerstner Jacobian Full",
        0.65, 7800, 4500)
    crest_width = scalar_parameter(
        material, editing, expressions, "SW Gerstner Crest Width",
        0.08, 7800, 4620)
    crest_sharpness = scalar_parameter(
        material, editing, expressions, "SW Gerstner Crest Sharpness",
        2.0, 7800, 4740)
    crest_min_amplitude = scalar_parameter(
        material, editing, expressions, "SW Gerstner Crest Min Wave Amplitude",
        50.0, 7800, 4860)
    compression_influence = scalar_parameter(
        material, editing, expressions, "SW Gerstner Compression Influence",
        0.35, 7800, 4980)

    compression = find_by_desc(expressions, CUSTOM_DESC)
    if compression is None:
        compression = find_by_desc(
            expressions, "SW Gerstner Jacobian Compression Mask")
    if compression is None:
        compression = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 8240, 4060)
        expressions.append(compression)
    inputs = [
        "WaterBodyIndex", "WaterTime", "LWCWorldPosition",
        "JacobianStart", "JacobianFull", "CrestWidth",
        "CrestSharpness", "CrestMinWaveAmplitude",
        "CompressionInfluence"]
    if not helper.configure_float1_custom_expression_with_includes(
            compression, inputs, CUSTOM_CODE, CUSTOM_DESC,
            ["/Project/SWGerstnerCompression.ush"]):
        raise RuntimeError("Could not configure compression Custom")

    connect(editing, water_index, "", compression, "WaterBodyIndex")
    connect(editing, water_time, "WaterTime", compression, "WaterTime")
    connect(editing, world_position, "XYZ", compression, "LWCWorldPosition")
    connect(editing, jacobian_start, "", compression, "JacobianStart")
    connect(editing, jacobian_full, "", compression, "JacobianFull")
    connect(editing, crest_width, "", compression, "CrestWidth")
    connect(editing, crest_sharpness, "", compression, "CrestSharpness")
    connect(editing, crest_min_amplitude, "", compression, "CrestMinWaveAmplitude")
    connect(editing, compression_influence, "", compression, "CompressionInfluence")

    if time_node is not None:
        editing.delete_material_expression(material, time_node)

    # The old raw-white Emissive preview is intentionally not recreated.
    debug_switch = find_parameter(expressions, "SW Gerstner Compression Debug")
    if debug_switch is not None:
        editing.delete_material_expression(material, debug_switch)

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("SW_GERSTNER_CREST_COMPRESSION_INTEGRATED=1")
    unreal.log("SW_GERSTNER_CREST_TIME_SOURCE=GetWaterTime")
    unreal.log("SW_GERSTNER_RAW_EMISSIVE_DEBUG=REMOVED")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
