"""Validate P1 graph invariants and request a real material recompile."""
import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def main():
    material = unreal.load_asset(ASSET_PATH)
    if material is None:
        raise RuntimeError("Could not load " + ASSET_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))
    by_name = {short_name(expression): expression for expression in expressions}

    water_output = by_name.get("MaterialExpressionSingleLayerWaterMaterialOutput_0")
    ocean_foam = by_name.get("MaterialExpressionCustom_16")
    web_foam = by_name.get("MaterialExpressionCustom_17")
    white_foam = by_name.get("MaterialExpressionCustom_15")
    if not all((water_output, ocean_foam, web_foam, white_foam)):
        raise RuntimeError("P1 validation graph anchors are missing")

    optics = helper.get_connected_input_expression(water_output, 0)
    absorption = helper.get_connected_input_expression(water_output, 1)
    phase_g = helper.get_connected_input_expression(water_output, 2)
    behind_water = helper.get_connected_input_expression(water_output, 3)
    if not isinstance(optics, unreal.MaterialExpressionCustom):
        raise RuntimeError("P1 Custom is not connected to scattering")
    if "SW_ApplyGerstnerCrestScattering" not in str(optics.get_editor_property("code")):
        raise RuntimeError("Unexpected Custom connected to scattering")
    if not isinstance(absorption, unreal.MaterialExpressionMultiply):
        raise RuntimeError("Short-path absorption Multiply is not connected")
    absorption_base = helper.get_connected_input_expression(absorption, 0)
    absorption_path = helper.get_connected_input_expression(absorption, 1)
    if short_name(absorption_base) != "MaterialExpressionMaterialFunctionCall_14":
        raise RuntimeError("Base absorption connection changed unexpectedly")
    if not isinstance(absorption_path, unreal.MaterialExpressionCustom):
        raise RuntimeError("Optical path Custom is missing")
    if "SW_ComputeGerstnerCrestOpticalPath" not in str(
            absorption_path.get_editor_property("code")):
        raise RuntimeError("Unexpected Custom connected as optical path")
    if helper.get_connected_input_expression(optics, 1) != absorption_path:
        raise RuntimeError("Scattering and absorption do not share the optical path")
    if phase_g is None or behind_water is None:
        raise RuntimeError("PhaseG or ColorScaleBehindWater was disconnected")

    foam_inputs = [helper.get_connected_input_expression(ocean_foam, index) for index in range(5)]
    if foam_inputs[0] != white_foam:
        raise RuntimeError("White foam sampler is not connected")
    if web_foam in foam_inputs:
        raise RuntimeError("Emerald web foam is still connected to final foam")
    if len([value for value in foam_inputs if value is not None]) != 5:
        raise RuntimeError("White foam Custom has a missing input")

    required_parameters = {
        "SW Crest Optics Bounds",
        "SW Crest Subsurface Tint",
        "SW Crest Shoulder Influence",
        "SW Crest Min Optical Path Scale",
        "SW Crest Subsurface Tint Strength",
        "SW Crest Optics Enabled",
    }
    present_parameters = set()
    for expression in expressions:
        try:
            present_parameters.add(str(expression.get_editor_property("parameter_name")))
        except Exception:
            pass
    missing_parameters = required_parameters - present_parameters
    if missing_parameters:
        raise RuntimeError("Missing P1 parameters: " + ", ".join(sorted(missing_parameters)))

    editing.recompile_material(material)
    unreal.log("SW_P1_GRAPH_VALIDATION=PASS")
    unreal.log("SW_P1_SCATTERING_NODE=" + short_name(optics))
    unreal.log("SW_P1_SHORT_PATH_ABSORPTION=" + short_name(absorption))
    unreal.log("SW_P1_WHITE_FOAM_PRESERVED=" + short_name(white_foam))
    unreal.log("SW_P1_EMERALD_WEB_DISCONNECTED=" + short_name(web_foam))


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
