"""Validate the height endpoint optics graph and compile the master material."""
import traceback

import unreal


MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
CUSTOM_DESC = "SW Wave Height Optical Endpoints"
OLD_PARAMETER_NAMES = {
    "SW Crest Optics Bounds", "SW Crest Subsurface Tint",
    "SW Crest Shoulder Influence", "SW Crest Min Optical Path Scale",
    "SW Crest Subsurface Tint Strength", "SW Crest Optics Enabled",
    "SW Crest Scattering Multiplier", "SW Crest Optics Strength",
}


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=""):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def main():
    master = unreal.load_asset(MASTER_PATH)
    if master is None:
        raise RuntimeError("Could not load " + MASTER_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(master))
    by_name = {short_name(e): e for e in expressions}

    multiply = by_name.get("MaterialExpressionMultiply_0")
    custom = helper.get_connected_input_expression(multiply, 0) if multiply else None
    if custom is None:
        raise RuntimeError("Wave-height optics Custom is missing")
    if "SW_InterpolateWaveHeightOptics" not in str(prop(custom, "code")):
        raise RuntimeError("Wave-height optics HLSL call is missing")

    output_names = [str(name) for name in helper.get_material_expression_output_names(custom)]
    for wanted in ("ScatteringA", "AbsorptionRGB", "AbsorptionA"):
        if wanted not in output_names:
            raise RuntimeError("Custom output is missing: " + wanted)
    for index in range(9):
        connected = helper.get_connected_input_expression(custom, index)
        unreal.log("SW_WAVE_HEIGHT_OPTICS_INPUT_{}={}".format(
            index, short_name(connected)))
        if connected is None:
            raise RuntimeError("Custom input {} is disconnected".format(index))

    divide_rgb = by_name.get("MaterialExpressionDivide_2")
    divide_a = by_name.get("MaterialExpressionDivide_1")
    coefficient_mask = by_name.get("MaterialExpressionMaterialFunctionCall_14")
    water_output = by_name.get("MaterialExpressionSingleLayerWaterMaterialOutput_0")
    if not all((multiply, divide_rgb, divide_a, coefficient_mask, water_output)):
        raise RuntimeError("Engine coefficient graph anchors are missing")
    if helper.get_connected_input_expression(multiply, 0) != custom:
        raise RuntimeError("Interpolated scattering RGB is not connected")
    if helper.get_connected_input_expression(multiply, 1) != custom:
        raise RuntimeError("Interpolated scattering A is not connected")
    if helper.get_connected_input_expression(divide_rgb, 1) != custom:
        raise RuntimeError("Interpolated absorption RGB is not connected")
    if helper.get_connected_input_expression(divide_a, 0) != divide_rgb:
        raise RuntimeError("Absorption reciprocal chain changed")
    if helper.get_connected_input_expression(divide_a, 1) != custom:
        raise RuntimeError("Interpolated absorption A is not connected")
    if helper.get_connected_input_expression(water_output, 0) != coefficient_mask:
        raise RuntimeError("CoefficientMask scattering does not feed SLW")
    if helper.get_connected_input_expression(water_output, 1) != coefficient_mask:
        raise RuntimeError("CoefficientMask absorption does not feed SLW")

    required = {
        "Scattering", "Absorption", "SW Crest Scattering",
        "SW Crest Absorption", "SW Wave Optics Height Range",
        "SW Wave Optics Enabled",
    }
    present = {str(prop(e, "parameter_name")) for e in expressions}
    missing = required - present
    if missing:
        raise RuntimeError("Required parameters missing: " + ", ".join(sorted(missing)))
    stale = OLD_PARAMETER_NAMES & present
    if stale:
        raise RuntimeError("Old bound-based parameters remain: " + ", ".join(sorted(stale)))
    for expression in expressions:
        code = str(prop(expression, "code"))
        if "SW_ComputeGerstnerCrestOpticalPath" in code or "SW_ApplyGerstnerCrestScattering" in code:
            raise RuntimeError("Old P1 HLSL Custom remains: " + short_name(expression))

    editing.recompile_material(master)
    unreal.log("SW_WAVE_HEIGHT_OPTICS_GRAPH_VALIDATION=PASS")
    unreal.log("SW_WAVE_HEIGHT_OPTICS_CUSTOM_OUTPUTS=" + ",".join(output_names))
    unreal.log("SW_WAVE_HEIGHT_OPTICS_ENGINE_PIPELINE_PRESERVED=1")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
