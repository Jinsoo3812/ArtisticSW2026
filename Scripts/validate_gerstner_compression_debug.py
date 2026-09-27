"""Validate the compile-time Gerstner Jacobian white debug path."""
import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
INSTANCE_PATH = "/Game/Blueprints/Water/M_Realistic_Water_Ocean"
SWITCH_NAME = "SW Gerstner Compression Debug"
CUSTOM_DESC = "SW Gerstner Jacobian Compression Mask"


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=""):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def main():
    material = unreal.load_asset(ASSET_PATH)
    instance = unreal.load_asset(INSTANCE_PATH)
    if material is None or instance is None:
        raise RuntimeError("Could not load water master and Ocean MI")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))
    by_name = {short_name(e): e for e in expressions}

    switch = next((e for e in expressions
                   if str(prop(e, "parameter_name")) == SWITCH_NAME), None)
    if not isinstance(switch, unreal.MaterialExpressionStaticSwitchParameter):
        raise RuntimeError("Compression debug Static Switch is missing")
    if bool(prop(switch, "default_value", True)):
        raise RuntimeError("Compression debug must default to Off")

    compression = helper.get_connected_input_expression(switch, 0)
    original_emissive = helper.get_connected_input_expression(switch, 1)
    if not isinstance(compression, unreal.MaterialExpressionCustom):
        raise RuntimeError("Compression Custom is not on the True branch")
    if original_emissive is None:
        raise RuntimeError("Existing Emissive was not preserved on the False branch")
    if str(prop(compression, "description")) != CUSTOM_DESC:
        raise RuntimeError("Unexpected Custom on compression branch")
    compression_code = str(prop(compression, "code"))
    if "SW_ComputeGerstnerCompressionMask" not in compression_code:
        raise RuntimeError("Compression HLSL call is missing")
    precise_xy = ("MakeLWCVector2(LWCLWCWorldPosition.Tile.xy, "
                  "LWCLWCWorldPosition.Offset.xy)")
    if precise_xy not in compression_code:
        raise RuntimeError("Compression HLSL is not passing precise FLWC XY")
    if "/Project/SWGerstnerCompression.ush" not in str(
            prop(compression, "include_file_paths")):
        raise RuntimeError("Compression include path is missing")
    for index in range(5):
        if helper.get_connected_input_expression(compression, index) is None:
            raise RuntimeError("Compression Custom input {} is disconnected".format(index))
    world_position = helper.get_connected_input_expression(compression, 2)
    world_mode = str(prop(world_position, "world_position_shader_offset"))
    if "EXCLUDE_ALL_SHADER_OFFSETS" not in world_mode.upper():
        raise RuntimeError(
            "Compression must use world position excluding WPO, got " + world_mode)

    final_attributes = by_name.get("MaterialExpressionSetMaterialAttributes_2")
    if final_attributes is None:
        raise RuntimeError("Final SetMaterialAttributes is missing")
    if helper.get_connected_input_expression(final_attributes, 6) != switch:
        raise RuntimeError("Debug switch is not connected to final Emissive")

    required = {
        SWITCH_NAME, "SW Gerstner Jacobian Start", "SW Gerstner Jacobian Full"}
    present = {str(prop(e, "parameter_name")) for e in expressions}
    missing = required - present
    if missing:
        raise RuntimeError("Debug parameters missing: " + ", ".join(sorted(missing)))

    duplicates = [e for e in expressions
                  if isinstance(e, unreal.MaterialExpressionCustom)
                  and "SW_ComputeGerstnerCompressionMask" in str(prop(e, "code"))]
    if len(duplicates) != 1:
        raise RuntimeError("Expected one compression Custom, found {}".format(len(duplicates)))

    if not editing.get_material_instance_static_switch_parameter_value(
            instance, SWITCH_NAME):
        raise RuntimeError("Ocean MI compression debug override is not enabled")

    editing.recompile_material(material)
    unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_VALIDATION=PASS")
    unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_TRUE=" + short_name(compression))
    unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_FALSE=" + short_name(original_emissive))
    unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_OCEAN_MI=ON")
    unreal.log("SW_GERSTNER_COMPRESSION_WORLD_POSITION=" + world_mode)


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
