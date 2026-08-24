"""Add a compile-time white Gerstner Jacobian debug view to the water master."""
import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
GROUP = "Gerstner Compression Debug"
INDEX_DESC = "SW Gerstner Compression Water Body Index"
TIME_DESC = "SW Gerstner Compression Time"
CUSTOM_DESC = "SW Gerstner Jacobian Compression Mask"
WORLD_DESC = "SW Gerstner Compression World Position (excluding WPO)"
SWITCH_NAME = "SW Gerstner Compression Debug"

CUSTOM_CODE = r"""return SW_ComputeGerstnerCompressionMask(
    WaterBodyIndex,
    Time,
    MakeLWCVector2(LWCLWCWorldPosition.Tile.xy, LWCLWCWorldPosition.Offset.xy),
    JacobianStart,
    JacobianFull);"""


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

    final_attributes = find_by_name(
        expressions, "MaterialExpressionSetMaterialAttributes_2")
    if final_attributes is None:
        raise RuntimeError("Final SetMaterialAttributes node is missing")

    debug_switch = find_parameter(expressions, SWITCH_NAME)
    if debug_switch is not None:
        original_emissive = helper.get_connected_input_expression(debug_switch, 1)
    else:
        # The final foam attribute node currently stores Emissive at input 6.
        original_emissive = helper.get_connected_input_expression(final_attributes, 6)
    if original_emissive is None:
        raise RuntimeError("Could not preserve the existing Emissive source")

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

    time_node = find_by_desc(expressions, TIME_DESC)
    if time_node is None:
        time_node = editing.create_material_expression(
            material, unreal.MaterialExpressionTime, 7800, 4220)
        time_node.set_editor_property("desc", TIME_DESC)
        time_node.set_editor_property("period", 0.0)
        expressions.append(time_node)

    jacobian_start = scalar_parameter(
        material, editing, expressions, "SW Gerstner Jacobian Start",
        0.70, 7800, 4380)
    jacobian_full = scalar_parameter(
        material, editing, expressions, "SW Gerstner Jacobian Full",
        0.25, 7800, 4500)

    compression = find_by_desc(expressions, CUSTOM_DESC)
    if compression is None:
        compression = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 8240, 4060)
        expressions.append(compression)
    inputs = [
        "WaterBodyIndex", "Time", "LWCWorldPosition",
        "JacobianStart", "JacobianFull"]
    if not helper.configure_float1_custom_expression_with_includes(
            compression, inputs, CUSTOM_CODE, CUSTOM_DESC,
            ["/Project/SWGerstnerCompression.ush"]):
        raise RuntimeError("Could not configure compression Custom")

    connect(editing, water_index, "", compression, "WaterBodyIndex")
    connect(editing, time_node, "", compression, "Time")
    connect(editing, world_position, "XYZ", compression, "LWCWorldPosition")
    connect(editing, jacobian_start, "", compression, "JacobianStart")
    connect(editing, jacobian_full, "", compression, "JacobianFull")

    if debug_switch is None:
        debug_switch = editing.create_material_expression(
            material, unreal.MaterialExpressionStaticSwitchParameter, 8660, 4060)
        debug_switch.set_editor_property("parameter_name", SWITCH_NAME)
        expressions.append(debug_switch)
    debug_switch.set_editor_property("default_value", False)
    debug_switch.set_editor_property("group", GROUP)
    debug_switch.set_editor_property("desc", "True: pure white Jacobian mask in Emissive")
    connect(editing, compression, "", debug_switch, "True")
    connect(editing, original_emissive, "", debug_switch, "False")

    if not helper.connect_emissive_attribute(final_attributes, debug_switch):
        raise RuntimeError("Could not reconnect final Emissive attribute")

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_INTEGRATED=1")
    unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_DEFAULT_OFF=1")
    unreal.log("SW_GERSTNER_COMPRESSION_EXISTING_EMISSIVE_PRESERVED=" + short_name(original_emissive))


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
