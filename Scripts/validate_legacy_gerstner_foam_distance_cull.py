"""Validate the active legacy emissive Gerstner foam distance-cull path."""
import traceback

import unreal


MATERIAL_PATH = "/Game/Blueprints/Water/M_Realistic_Water"


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=None):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def main():
    material = unreal.load_asset(MATERIAL_PATH)
    helper = unreal.RealisticWaterMaterialPipelineLibrary
    expressions = list(helper.get_material_expressions(material))
    legacy = next((expression for expression in expressions
                   if short_name(expression) == "MaterialExpressionCustom_16"), None)
    if legacy is None:
        raise RuntimeError("Legacy Custom_16 is missing")
    if "SW_ApplyLegacyGerstnerFoamDistanceCull" not in str(prop(legacy, "code", "")):
        raise RuntimeError("Legacy Custom does not apply camera-distance culling")
    if "/Project/SWFluxOceanFoam.ush" not in str(
            prop(legacy, "include_file_paths", "")):
        raise RuntimeError("Legacy Custom does not include SWFluxOceanFoam.ush")
    for index in range(7):
        if helper.get_connected_input_expression(legacy, index) is None:
            raise RuntimeError("Legacy Custom input {} is disconnected".format(index))

    final_attributes = next((expression for expression in expressions
                             if short_name(expression) ==
                             "MaterialExpressionSetMaterialAttributes_2"), None)
    if final_attributes is None:
        raise RuntimeError("Final material attributes node is missing")
    if helper.get_connected_input_expression(final_attributes, 6) != legacy:
        raise RuntimeError("Legacy Custom is not connected to final Emissive")

    parameter = next((expression for expression in expressions
                      if str(prop(expression, "parameter_name", "")) ==
                      "Legacy Gerstner Foam Cull Distance"), None)
    if parameter is None or abs(float(prop(parameter, "default_value", 0.0)) - 50000.0) > 0.1:
        raise RuntimeError("Legacy foam cull-distance default is not 50000 cm")

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log("LEGACY_GERSTNER_FOAM_DISTANCE_CULL_VALIDATION=PASS")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
